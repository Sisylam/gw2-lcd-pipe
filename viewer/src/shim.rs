//! Reader for the `Local\GW2LCDShim` shared-memory framebuffer.
//!
//! Layout (little-endian): a 48-byte header of twelve `u32`s, then the pixels -
//! `width*height*3` RGB bytes for the colour panel, or `width*height` bytes for
//! the monochrome panel. Mirrors the original `lcdshim.py`.

use crate::win::{self, HANDLE};

pub const MAGIC: u32 = 0x3231_3047; // "G012"
pub const VERSION: u32 = 1;
pub const HDR_SIZE: usize = 12 * 4;
pub const MAX_PIXELS: usize = 320 * 240 * 3;

const SHM_NAME: &str = "Local\\GW2LCDShim";

/// One decoded shim header plus its pixels.
#[allow(dead_code)] // some header fields are informational only
pub struct Frame {
    pub width: u32,
    pub height: u32,
    pub bpp: u32,
    pub active: u32, // 1 = colour, 2 = mono
    pub sequence: u32,
    pub connected: u32,
    pub init_count: u32,
    pub text_count: u32,
    pub update_count: u32,
    pub rgb: Vec<u8>,
    pub mono: Vec<u8>,
}

pub struct Shim {
    h: HANDLE,
    view: *mut u8,
    last_seq: i64,
}

impl Shim {
    pub fn new() -> Self {
        Shim {
            h: std::ptr::null_mut(),
            view: std::ptr::null_mut(),
            last_seq: -1,
        }
    }

    pub fn is_open(&self) -> bool {
        !self.view.is_null()
    }

    /// Open the section if it exists. Returns true when mapped.
    pub fn open(&mut self) -> bool {
        if self.is_open() {
            return true;
        }
        let name = win::wide(SHM_NAME);
        let h = unsafe { win::OpenFileMappingW(win::FILE_MAP_READ, 0, name.as_ptr()) };
        if h.is_null() {
            return false;
        }
        let view = unsafe {
            win::MapViewOfFile(h, win::FILE_MAP_READ, 0, 0, HDR_SIZE + MAX_PIXELS)
        } as *mut u8;
        if view.is_null() {
            unsafe { win::CloseHandle(h) };
            return false;
        }
        self.h = h;
        self.view = view;
        true
    }

    pub fn close(&mut self) {
        if !self.view.is_null() {
            unsafe { win::UnmapViewOfFile(self.view as *const _) };
            self.view = std::ptr::null_mut();
        }
        if !self.h.is_null() {
            unsafe { win::CloseHandle(self.h) };
            self.h = std::ptr::null_mut();
        }
    }

    fn u32_at(&self, off: usize) -> u32 {
        let mut b = [0u8; 4];
        unsafe {
            std::ptr::copy_nonoverlapping(self.view.add(off), b.as_mut_ptr(), 4);
        }
        u32::from_le_bytes(b)
    }

    /// Decode the current frame, or `None` if the section is not a valid shim.
    pub fn read(&mut self) -> Option<Frame> {
        if !self.is_open() {
            return None;
        }
        let magic = self.u32_at(0);
        let version = self.u32_at(4);
        if magic != MAGIC || version != VERSION {
            return None;
        }
        let width = self.u32_at(8);
        let height = self.u32_at(12);
        let bpp = self.u32_at(16);
        let active = self.u32_at(20);
        let sequence = self.u32_at(24);
        let connected = self.u32_at(28);
        let init_count = self.u32_at(32);
        let text_count = self.u32_at(36);
        let update_count = self.u32_at(40);

        let mut f = Frame {
            width,
            height,
            bpp,
            active,
            sequence,
            connected,
            init_count,
            text_count,
            update_count,
            rgb: Vec::new(),
            mono: Vec::new(),
        };

        let px = unsafe { self.view.add(HDR_SIZE) };
        if active == 1 && width > 0 && height > 0 {
            let n = (width as usize * height as usize * 3).min(MAX_PIXELS);
            f.rgb = unsafe { std::slice::from_raw_parts(px, n) }.to_vec();
        } else if active == 2 && width > 0 && height > 0 {
            let n = width as usize * height as usize;
            f.mono = unsafe { std::slice::from_raw_parts(px, n) }.to_vec();
        }
        Some(f)
    }

    /// True once per new frame, keyed on the sequence counter.
    pub fn is_new(&mut self, f: &Frame) -> bool {
        if f.sequence as i64 == self.last_seq {
            return false;
        }
        self.last_seq = f.sequence as i64;
        true
    }
}

impl Drop for Shim {
    fn drop(&mut self) {
        self.close();
    }
}
