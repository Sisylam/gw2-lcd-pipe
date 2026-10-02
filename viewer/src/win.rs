//! Minimal Win32 FFI for the two named shared-memory channels.
//!
//! Only a handful of kernel32 entry points are needed, so they are declared by
//! hand rather than pulling in a bindings crate. The shim publishes frames to
//! `Local\GW2LCDShim` (read-only for us) and the server watches
//! `Local\LGLCDCtl` (read/write for us) for button requests.

#![allow(non_snake_case)]

use std::ffi::c_void;

// Mirrors the Win32 type name on purpose.
#[allow(clippy::upper_case_acronyms)]
pub type HANDLE = *mut c_void;

pub const FILE_MAP_READ: u32 = 0x0004;
pub const FILE_MAP_ALL_ACCESS: u32 = 0x000F_001F;

extern "system" {
    pub fn OpenFileMappingW(
        dwDesiredAccess: u32,
        bInheritHandle: i32,
        lpName: *const u16,
    ) -> HANDLE;

    pub fn MapViewOfFile(
        hFileMappingObject: HANDLE,
        dwDesiredAccess: u32,
        dwFileOffsetHigh: u32,
        dwFileOffsetLow: u32,
        dwNumberOfBytesToMap: usize,
    ) -> *mut c_void;

    pub fn UnmapViewOfFile(lpBaseAddress: *const c_void) -> i32;

    pub fn CloseHandle(hObject: HANDLE) -> i32;
}

/// Encode a `&str` as a NUL-terminated UTF-16 buffer.
pub fn wide(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(std::iter::once(0)).collect()
}
