// Hide the console window: without this the GNU build is a console-subsystem
// binary and Windows attaches a command prompt behind the window.
#![windows_subsystem = "windows"]

//! Native viewer for the GW2 LCD shim.
//!
//! Shows whatever GW2 renders into `Local\GW2LCDShim` and provides the six
//! G19S soft buttons. Button presses go through `Local\LGLCDCtl` to the
//! replacement server, which turns them into `0x0702` notifications. This
//! replaces the original Python viewer and reader with a single native
//! executable.

mod ctl;
mod shim;
mod win;

use font8x8::UnicodeFonts;
use minifb::{Key, MouseButton, MouseMode, Window, WindowOptions};

const W: usize = 320;
const IMG_H: usize = 240;
const BAR_H: usize = 30;
const H: usize = IMG_H + BAR_H;

const BTN_W: usize = 48;
const BTN_GAP: usize = 4;
const BTN_X0: usize = 4;
const BAR_PAD: usize = 3;

const COLOR_BG: u32 = 0x0010_1014;
const COLOR_BTN: u32 = 0x002A_2A34;
const COLOR_BTN_HOT: u32 = 0x003A_3A48;
const COLOR_TEXT: u32 = 0x00E8_E8F0;

struct Button {
    bit: u32,
    label: &'static str,
    x: usize,
}

fn make_buttons() -> Vec<Button> {
    const DEFS: [(u32, &str); 6] = [
        (ctl::BTN_PREV, "<"),
        (ctl::BTN_NEXT, ">"),
        (ctl::BTN_OK, "ok"),
        (ctl::BTN_UP, "^"),
        (ctl::BTN_DOWN, "v"),
        (ctl::BTN_MENU, "menu"),
    ];
    DEFS.iter()
        .enumerate()
        .map(|(i, &(bit, label))| Button {
            bit,
            label,
            x: BTN_X0 + i * (BTN_W + BTN_GAP),
        })
        .collect()
}

fn draw_text(buf: &mut [u32], x: usize, y: usize, text: &str, color: u32) {
    let mut cx = x;
    for ch in text.chars() {
        if let Some(glyph) = font8x8::BASIC_FONTS.get(ch) {
            for (row, bits) in glyph.iter().enumerate() {
                for col in 0..8 {
                    if bits & (1 << col) != 0 {
                        let px = cx + col;
                        let py = y + row;
                        if px < W && py < H {
                            buf[py * W + px] = color;
                        }
                    }
                }
            }
        }
        cx += 8;
    }
}

/// Scale a source image (w*h, `stride` bytes per pixel) into the 320x240 area.
fn blit(img: &mut [u32], f: &shim::Frame) {
    let w = f.width as usize;
    let h = f.height as usize;
    if w == 0 || h == 0 {
        return;
    }
    let color = f.active == 1 && f.rgb.len() >= w * h * 3;
    let mono = f.active == 2 && f.mono.len() >= w * h;
    if !color && !mono {
        return;
    }
    for y in 0..IMG_H {
        let sy = y * h / IMG_H;
        for x in 0..W {
            let sx = x * w / W;
            let c = if color {
                let i = (sy * w + sx) * 3;
                let r = f.rgb[i] as u32;
                let g = f.rgb[i + 1] as u32;
                let b = f.rgb[i + 2] as u32;
                (r << 16) | (g << 8) | b
            } else {
                let v = f.mono[sy * w + sx];
                if v != 0 {
                    0x00FF_FFFF
                } else {
                    0x0000_0000
                }
            };
            img[y * W + x] = c;
        }
    }
}

fn in_bar(_mx: f32, my: f32) -> bool {
    my >= (IMG_H + BAR_PAD) as f32 && my < (IMG_H + BAR_H - BAR_PAD) as f32
}

fn hit(buttons: &[Button], mx: f32, my: f32) -> Option<u32> {
    if !in_bar(mx, my) {
        return None;
    }
    buttons
        .iter()
        .find(|b| mx >= b.x as f32 && mx < (b.x + BTN_W) as f32)
        .map(|b| b.bit)
}

fn draw_bar(buf: &mut [u32], buttons: &[Button], mouse: Option<(f32, f32)>) {
    let y0 = IMG_H + BAR_PAD;
    let y1 = IMG_H + BAR_H - BAR_PAD;
    for p in buf[IMG_H * W..].iter_mut() {
        *p = COLOR_BG;
    }
    for b in buttons {
        let hot = mouse.is_some_and(|(mx, my)| {
            in_bar(mx, my) && mx >= b.x as f32 && mx < (b.x + BTN_W) as f32
        });
        let color = if hot { COLOR_BTN_HOT } else { COLOR_BTN };
        for y in y0..y1 {
            for x in b.x..b.x + BTN_W {
                buf[y * W + x] = color;
            }
        }
        let tw = b.label.chars().count() * 8;
        let tx = b.x + (BTN_W.saturating_sub(tw)) / 2;
        let ty = y0 + ((y1 - y0).saturating_sub(8)) / 2;
        draw_text(buf, tx, ty, b.label, COLOR_TEXT);
    }
}

fn main() {
    let mut window = Window::new(
        "GW2 LCD viewer",
        W,
        H,
        WindowOptions::default(),
    )
    .expect("create window");
    window.set_target_fps(30);

    let buttons = make_buttons();
    let mut shim = shim::Shim::new();
    let mut img = vec![COLOR_BG; W * IMG_H];
    let mut buf = vec![COLOR_BG; W * H];
    let mut title = String::from("GW2 LCD viewer - waiting for GW2");
    let mut prev_down = false;
    let mut prev_keys: (bool, bool) = (false, false);

    while window.is_open() && !window.is_key_down(Key::Escape) {
        // --- poll the shim ---
        if !shim.is_open() {
            shim.open();
        }
        if shim.is_open() {
            if let Some(f) = shim.read() {
                if shim.is_new(&f) {
                    blit(&mut img, &f);
                    title = format!(
                        "GW2 LCD viewer - {}x{} seq={} init={} text={} upd={}",
                        f.width, f.height, f.sequence, f.init_count,
                        f.text_count, f.update_count
                    );
                }
            } else {
                title = "GW2 LCD viewer - shim mapped, bad header".into();
            }
        } else {
            title = "GW2 LCD viewer - no shim in shared memory (is GW2 running?)".into();
        }

        // --- compose the frame ---
        buf[..W * IMG_H].copy_from_slice(&img);
        let mouse = window.get_mouse_pos(MouseMode::Clamp);
        draw_bar(&mut buf, &buttons, mouse);

        // --- input: mouse clicks ---
        let down = window.get_mouse_down(MouseButton::Left);
        if down && !prev_down {
            if let Some((mx, my)) = mouse {
                if let Some(bit) = hit(&buttons, mx, my) {
                    match ctl::post(bit) {
                        Ok(()) => title = format!("GW2 LCD viewer - sent {}", ctl::name(bit)),
                        Err(e) => title = format!("GW2 LCD viewer - {e}"),
                    }
                }
            }
        }
        prev_down = down;

        // --- input: keyboard shortcuts ---
        let left = window.is_key_down(Key::Left);
        let right = window.is_key_down(Key::Right);
        if left && !prev_keys.0 {
            let _ = ctl::post(ctl::BTN_PREV);
            title = "GW2 LCD viewer - sent <".into();
        }
        if right && !prev_keys.1 {
            let _ = ctl::post(ctl::BTN_NEXT);
            title = "GW2 LCD viewer - sent >".into();
        }
        prev_keys = (left, right);

        window.set_title(&title);
        window
            .update_with_buffer(&buf, W, H)
            .expect("update buffer");
    }
}
