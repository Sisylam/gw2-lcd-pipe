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
        let view = unsafe { win::MapViewOfFile(h, win::FILE_MAP_READ, 0, 0, HDR_SIZE + MAX_PIXELS) }
            as *mut u8;
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

    /// Decode the current frame, or `None` if the section is not a valid shim.
    pub fn read(&mut self) -> Option<Frame> {
        if !self.is_open() {
            return None;
        }
        let view = unsafe { std::slice::from_raw_parts(self.view, HDR_SIZE + MAX_PIXELS) };
        parse(view)
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

/// Decode a shim view. Pure, so it can be unit-tested without a mapping.
fn parse(view: &[u8]) -> Option<Frame> {
    if view.len() < HDR_SIZE {
        return None;
    }
    let u32_at =
        |off: usize| u32::from_le_bytes([view[off], view[off + 1], view[off + 2], view[off + 3]]);
    if u32_at(0) != MAGIC || u32_at(4) != VERSION {
        return None;
    }

    let width = u32_at(8);
    let height = u32_at(12);
    let bpp = u32_at(16);
    let active = u32_at(20);
    let sequence = u32_at(24);
    let connected = u32_at(28);
    let init_count = u32_at(32);
    let text_count = u32_at(36);
    let update_count = u32_at(40);

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

    let px = &view[HDR_SIZE..];
    if active == 1 && width > 0 && height > 0 {
        let n = (width as usize * height as usize * 3)
            .min(MAX_PIXELS)
            .min(px.len());
        f.rgb = px[..n].to_vec();
    } else if active == 2 && width > 0 && height > 0 {
        let n = (width as usize * height as usize).min(px.len());
        f.mono = px[..n].to_vec();
    }
    Some(f)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn buf(magic: u32, ver: u32, w: u32, h: u32, bpp: u32, active: u32) -> Vec<u8> {
        let mut v = vec![0u8; HDR_SIZE + MAX_PIXELS];
        v[0..4].copy_from_slice(&magic.to_le_bytes());
        v[4..8].copy_from_slice(&ver.to_le_bytes());
        v[8..12].copy_from_slice(&w.to_le_bytes());
        v[12..16].copy_from_slice(&h.to_le_bytes());
        v[16..20].copy_from_slice(&bpp.to_le_bytes());
        v[20..24].copy_from_slice(&active.to_le_bytes());
        v
    }

    #[test]
    fn rejects_wrong_magic_or_version() {
        assert!(parse(&buf(0, VERSION, 320, 240, 24, 1)).is_none());
        assert!(parse(&buf(MAGIC, 99, 320, 240, 24, 1)).is_none());
    }

    #[test]
    fn rejects_short_buffer() {
        assert!(parse(&[0u8; 8]).is_none());
    }

    #[test]
    fn parses_colour_frame() {
        let mut v = buf(MAGIC, VERSION, 320, 240, 24, 1);
        v[HDR_SIZE] = 0x30;
        v[HDR_SIZE + 1] = 0x20;
        v[HDR_SIZE + 2] = 0x10;
        let f = parse(&v).expect("colour frame");
        assert_eq!((f.width, f.height, f.bpp, f.active), (320, 240, 24, 1));
        assert_eq!(f.rgb.len(), 320 * 240 * 3);
        assert_eq!(&f.rgb[..3], &[0x30, 0x20, 0x10]);
        assert!(f.mono.is_empty());
    }

    #[test]
    fn parses_mono_frame() {
        let mut v = buf(MAGIC, VERSION, 160, 43, 1, 2);
        v[HDR_SIZE] = 0xff;
        let f = parse(&v).expect("mono frame");
        assert_eq!((f.width, f.height, f.active), (160, 43, 2));
        assert_eq!(f.mono.len(), 160 * 43);
        assert_eq!(f.mono[0], 0xff);
        assert!(f.rgb.is_empty());
    }

    #[test]
    fn zero_size_parses_without_pixels() {
        let v = buf(MAGIC, VERSION, 0, 0, 0, 1);
        let f = parse(&v).expect("header is valid");
        assert!(f.rgb.is_empty());
        assert!(f.mono.is_empty());
    }
}
