//! Client of the `Local\LGLCDCtl` view-button channel.
//!
//! A press bumps a `request` counter; the server notices the change and spends
//! a `0x0702` press+release on the next read GW2 is already waiting on. The
//! mapping is opened per press on purpose - the server may not be running yet,
//! and a stale mapping would silently swallow requests. Mirrors the control
//! channel of the original Python viewer.

use crate::win::{self, HANDLE};

pub const CTL_MAGIC: u32 = 0x3154_4347; // "GCT1"
pub const CTL_BYTES: usize = 32;
const CTL_NAME: &str = "Local\\LGLCDCtl";

pub const BTN_PREV: u32 = 0x0000_0100; // [<]
pub const BTN_NEXT: u32 = 0x0000_0200; // [>]
pub const BTN_OK: u32 = 0x0000_0400; // [ok]
pub const BTN_UP: u32 = 0x0000_1000; // [^]
pub const BTN_DOWN: u32 = 0x0000_2000; // [v]
pub const BTN_MENU: u32 = 0x0000_4000; // [menu]

/// Human name for a button bit, for status text.
pub fn name(button: u32) -> &'static str {
    match button {
        BTN_PREV => "<",
        BTN_NEXT => ">",
        BTN_OK => "ok",
        BTN_UP => "^",
        BTN_DOWN => "v",
        BTN_MENU => "menu",
        _ => "?",
    }
}

struct Mapping {
    h: HANDLE,
    view: *mut u32,
}

impl Mapping {
    fn open() -> Option<Mapping> {
        let name = win::wide(CTL_NAME);
        let h = unsafe { win::OpenFileMappingW(win::FILE_MAP_ALL_ACCESS, 0, name.as_ptr()) };
        if h.is_null() {
            return None;
        }
        let view = unsafe {
            win::MapViewOfFile(h, win::FILE_MAP_ALL_ACCESS, 0, 0, CTL_BYTES)
        } as *mut u32;
        if view.is_null() {
            unsafe { win::CloseHandle(h) };
            return None;
        }
        Some(Mapping { h, view })
    }

    fn close(&self) {
        unsafe {
            win::UnmapViewOfFile(self.view as *const _);
            win::CloseHandle(self.h);
        }
    }
}

/// Post one soft-button request. Returns a short status string.
pub fn post(button: u32) -> Result<(), String> {
    let m = Mapping::open().ok_or_else(|| {
        "Local\\LGLCDCtl not open - is lgpipe_server running?".to_string()
    })?;
    let result = unsafe {
        if *m.view != CTL_MAGIC {
            Err("bad magic in control mapping".to_string())
        } else {
            let request = (*m.view.add(1)).wrapping_add(1);
            *m.view.add(1) = request;
            *m.view.add(2) = button;
            Ok(())
        }
    };
    m.close();
    result
}
