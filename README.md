# STM32F446 Custom Bootloader

A bare-metal **multi-application bootloader with UART firmware updates**, featuring fixed Flash partitioning, application selection, manual vector-table-based handoff, per-image metadata headers, CRC32 validation, and a **UART-based firmware update protocol** with a shared bootloader API table exposed to applications through a custom linker section.

This project was built to understand what actually happens between **reset → bootloader → application → interrupt handling → firmware update**, rather than treating the boot process as a black box.

> **Note on target hardware:** this project's compiler defines and CMSIS headers currently target `STM32F411RETx`, but development and testing have been done on an **STM32F446RE** board. Both parts share the same Cortex-M4F core and a compatible Flash/USART register layout, so the code runs correctly, but the project defines should be retargeted to F446 before relying on any F411-specific peripheral behavior. Flagged here as a known cleanup item.

---

## Project Overview

The system uses internal Flash to host:

- A **bootloader**
- A **default application**
- An **Application 1**
- A **factory/recovery application** (permanently update-protected)
- A **shared bootloader API table**

At startup, the bootloader checks a hardware button:

- **Button not pressed** → launch the default application
- **Button pressed** → enter a UART-based bootloader menu

```text
1       ==> Run App 1
F       ==> Run Factory Application
U       ==> Enter Firmware Update Mode
Other   ==> Default Application
```

The bootloader manually reads the selected application's vector table, loads its initial Main Stack Pointer, obtains its `Reset_Handler`, and branches to it — but only after validating that application's image through a per-image header.

---

## Why This Project?

A microcontroller does not simply "run `main()`" after reset, and a bootloader does not simply "trust whatever is in Flash." This project was built to implement, by hand, the full chain of what makes a firmware handoff both *possible* and *safe*:

```text
Reset → Vector Table → Initial MSP → Reset_Handler → SystemInit()
      → Runtime init → main()
```

and, layered on top of that:

```text
Image Header → Magic Check → Size Check → CRC32 Check → Vector Table Read → Jump
```

Core concepts covered:

- ARM Cortex-M startup, vector tables, VTOR relocation
- Linker scripts, custom sections, Flash memory organization
- Function pointers, interrupts, UART RX (interrupt-driven and polled)
- SysTick, memory-mapped peripherals
- Bootloader/application separation and shared firmware interfaces
- **Per-image metadata headers and CRC32 image validation**
- **Build-time tooling for size/CRC/version stamping**
- **A framed UART protocol for in-field firmware updates**
- **Power-loss-safe update ordering**

---

# System Architecture

```text
                   STM32F446RE
              ┌───────────────────────┐
              │       Flash           │
0x08000000 ───► Bootloader             │
0x08004000 ───► Default Application    │ (512B header + vector table + code)
0x08008000 ───► Application 1          │ (512B header + vector table + code)
0x0800C000 ───► Factory Application    │ (512B header + vector table + code) — update-protected
0x08010000 ───► Common API Table       │
              └───────────────────────┘
```

Each application slot is laid out internally as:

```text
<slot base>        → Application Header (512 bytes)
<slot base>+0x200   → Vector Table (Initial MSP, Reset_Handler, ...)
<slot base>+0x200+  → Application code (.text / .data / .bss)
```

---

# Flash Memory Map

| Region      | Start Address |  Size | Purpose                                  |
| ----------- | ------------: | ----: | ----------------------------------------- |
| Bootloader  |  `0x08000000` | 16 KB | Boot, validation, selection, UART update  |
| Default App |  `0x08004000` | 16 KB | Normal application (header + code)        |
| App 1       |  `0x08008000` | 16 KB | Alternate application (header + code)     |
| Factory App |  `0x0800C000` | 16 KB | Recovery image — permanently protected    |
| Common API  |  `0x08010000` |     — | Shared bootloader API table               |

Each 16 KB slot reserves its first 512 bytes (`0x200`) for the application header, aligned to this boundary specifically because the Cortex-M4's VTOR register requires alignment at least as large as the implemented vector table — 512 bytes on this part.

---

# Application Header & Image Validation

Every application carries a fixed-layout metadata header immediately before its vector table:

```c
#define APP_HEADER_MAGIC   0xB00710ADUL
#define APP_HEADER_SIZE    0x200UL

typedef struct
{
    uint32_t magic;
    uint32_t image_size;
    uint32_t crc32;
    uint32_t version;
    uint32_t reserved[124];
} app_header_t;
```

Placed via a dedicated linker section, which also enforces the required alignment:

```ld
.app_header :
{
    . = ALIGN(4);
    KEEP(*(.app_header))
    . = ALIGN(0x200);
} >FLASH

.isr_vector :
{
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
} >FLASH
```

Before jumping to any application, the bootloader validates in three stages, cheapest first:

```text
magic == APP_HEADER_MAGIC?  → reject erased/empty slots instantly
image_size within bounds?   → reject nonsensical sizes before a CRC pass
crc32 matches computed CRC? → reject corrupted or incomplete images
```

Only an image that passes all three ever has its vector table read.

---

# Build-Time Tooling

`image_size` and `crc32` can't be known until a binary exists, so a post-build Python script (`patch_header.py`) runs after every compile:

- Computes the final `.bin` size and patches it into the header
- Computes CRC32 over the image (with the CRC field itself zeroed during computation) and patches the real value in
- Maintains a per-project `version.txt`, incrementing the firmware version on every build

Wired into CubeIDE's post-build steps alongside `arm-none-eabi-objcopy`, so every build automatically produces a correctly patched, versioned `.bin` with no manual step required.

---

# UART Firmware Update Protocol

A framed protocol allows a host PC to push a new application into **`default_app`** or **`app1`** over UART — `factory_app` is permanently excluded, guaranteeing a known-good recovery path always exists.

**Frame format**, both directions:

```text
SOF(1) CMD(1) LEN(2, LE) PAYLOAD(LEN) CRC16(2, LE)
```

**Commands:**

```text
START_UPDATE   Host selects a target slot by ID (not a raw address)
DATA           One chunk of image bytes
END_UPDATE     Triggers a full-image CRC32 check and finalizes
ACK / NACK     Bootloader's response, with an error code on NACK
```

**Exchange:**

```text
Host                                    Bootloader
  │── START_UPDATE(slot) ──────────────►│  erase sector, zero magic word
  │◄──────────────────────────── ACK ───│
  │── DATA(chunk) ─────────────────────►│  write to Flash        (repeated)
  │◄────────────────────────── ACK/NACK─│
  │── END_UPDATE ──────────────────────►│  verify full image CRC32
  │◄──────────────────────────── ACK ───│  write real magic word (final step)
```

**Power-loss safety:** the header's magic word is zeroed the moment a sector is erased, and only written back to its real value as the *last* action of `END_UPDATE`, after the full-image CRC passes. An interrupted update — power loss or a dropped connection at any point — leaves the slot failing the normal magic check on next boot, identical to the rejection path already used for corrupted images.

The host-side Python script (`flash_update.py`) frames a patched `.bin` into chunks and drives the exchange, retrying a timed-out frame before giving up, since a UART-over-USB link isn't perfectly reliable.

---

# Shared Bootloader API

The bootloader exposes selected functionality to applications through a function-pointer table placed at a fixed Flash address, avoiding duplicated implementation across images:

```c
struct btl_common_apis
{
    void (*led_init)(void);
    void (*led_toggle)(uint32_t dly);
    void (*led_on)(void);
    void (*led_off)(void);
    void (*debug_uart_init)(void);
    void (*button_init)(void);
    bool (*get_btn_state)(void);
    void (*fpu_enable)(void);
    void (*timebase_init)(void);
};
```

Placed via `__attribute__((section(".COMMON_APIS")))` at `0x08010000`, and read by applications through a fixed-address pointer. The address the bootloader's linker places this table at, and the address applications hardcode to read it from, must agree exactly — there's no symbol resolution across independently linked images, so this agreement is the entire contract.

---

# Project Structure

```text
stm32_custom_bootloader/
│
├── README.md
├── patch_header.py
├── flash_update.py
│
├── bootloader/
│   ├── Src/
│   │   ├── main.c
│   │   ├── update_protocol.c
│   │   ├── crc32.c
│   │   ├── crc16.c
│   │   ├── flash_prog.c
│   │   └── bsp.c / fpu.c / uart.c / timebase.c
│   └── Inc/
│       ├── app_header.h
│       ├── common_apis.h
│       ├── update_protocol.h
│       ├── crc32.h / crc16.h / flash_prog.h
│
├── default_app/
├── app1/
├── factory_app/
│   └── (each: Src/main.c with app_header instance, STM32F4xx_FLASH.ld with .app_header section)
│
└── docs/
    └── architecture.md
```

---

# Technologies

**Microcontroller:** STM32F446RE, ARM Cortex-M4F
**Programming:** Embedded C, direct register-level programming
**Peripherals:** GPIO, USART2, SysTick, NVIC, FPU, Flash controller
**Toolchain:** GCC, GNU Linker, custom linker sections, Python (build tooling + host update script)

---

# Key Embedded Concepts Demonstrated

**Cortex-M Startup:** vector tables, initial MSP, Reset Handler, VTOR relocation
**Memory Management:** Flash partitioning, linker scripts, section placement, `.data`/`.bss`
**Interrupt Architecture:** NVIC, USART interrupts, SysTick, ISR-vs-polling resource ownership
**Embedded C:** function pointers, `volatile`, `static`, memory-mapped registers, custom GCC attributes
**Bootloader Design:** application selection, header-based validation, CRC32, versioning, shared API interfaces
**Firmware Update Design:** framed serial protocols, ACK/NACK/retry, power-loss-safe commit ordering, update-protected recovery images

---

# What I Learned

The project moved my understanding from:

> "The MCU starts executing `main()`."

to:

> "The MCU starts from a vector table, obtains the initial stack pointer, enters `Reset_Handler`, initializes the runtime, and executes application code — code the bootloader has already verified is intact, correctly sized, and not corrupted."

And further, with the update protocol:

> "A bootloader isn't just a selector between known-good images — it can safely accept a *new*, previously-unverified image over an unreliable link, and guarantee that a failure partway through never results in a half-written image being trusted."

---

# Future Improvements

- [ ] Version-aware rollback protection (version is tracked and reported, not yet enforced against)
- [ ] Image authentication / signature verification
- [ ] Automated multi-image build pipeline
- [ ] Retarget project compiler defines from F411 to F446 to match actual hardware
- [ ] Watchdog-based recovery
- [ ] Boot reason detection
- [ ] Strong MSP and Reset_Handler bounds validation

---

## Author

**Raghav Agrawal**
