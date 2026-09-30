# STM32F446 Custom Bootloader

A bare-metal **multi-application bootloader for STM32F446RETx** featuring fixed Flash partitioning, application selection, manual vector-table-based handoff, UART command control, and a **shared bootloader API table** exposed to applications through a custom linker section.

This project was built to understand what actually happens between **reset → bootloader → application → interrupt handling**, rather than treating the boot process as a black box.

---

## Project Overview

The system uses the STM32F446's internal Flash to host:

- A **bootloader**
- A **default application**
- An **Application 1**
- A **factory/recovery application**
- A **shared bootloader API table**

At startup, the bootloader checks a hardware button:

- **Button not pressed** → launch the default application
- **Button pressed** → enter a UART-based bootloader menu

The user can then select:

```text
1 → Run Application 1
F → Run Factory Application
Other → Default Application
```

The bootloader manually reads the selected application's vector table, loads its initial Main Stack Pointer, obtains its `Reset_Handler`, and branches to it.

The applications then relocate the **Vector Table Offset Register (VTOR)** to their own Flash region and use common services exported by the bootloader.

---

## Why This Project?

A microcontroller does not simply "run `main()`" after reset.

A real embedded boot flow involves:

```text
Reset
  ↓
Vector Table
  ↓
Initial MSP
  ↓
Reset_Handler
  ↓
SystemInit()
  ↓
Runtime initialization
  ↓
main()
```

This project was built to understand and implement that flow manually.

It focuses on several core embedded concepts:

- ARM Cortex-M startup
- Vector tables
- `Reset_Handler`
- MSP initialization
- Function pointers
- VTOR relocation
- Linker scripts
- Flash memory organization
- Custom linker sections
- Interrupts
- UART RX interrupts
- SysTick
- Memory-mapped peripherals
- Bootloader/application separation
- Shared firmware interfaces
- Fixed-address application execution

---

# System Architecture

```text
                   STM32F446RETx
              ┌───────────────────────┐
              │       Flash           │
              │                       │
0x08000000 ───► Bootloader             │
              │                       │
0x08004000 ───► Default Application    │
              │                       │
0x08008000 ───► Application 1          │
              │                       │
0x0800C000 ───► Factory Application    │
              │                       │
0x08010000 ───► Common API Table       │
              │                       │
              └───────────────────────┘
```

RAM:

```text
0x20000000 ──────────────── RAM start
             Application /
             bootloader data
             stack
             heap
0x20020000 ──────────────── RAM end
```

---

# Flash Memory Map

The logical firmware layout used in this project is:

| Region      | Start Address |  Size | Purpose                        |
| ----------- | ------------: | ----: | ------------------------------ |
| Bootloader  |  `0x08000000` | 32 KB | Boot and application selection |
| Default App |  `0x08004000` | 32 KB | Normal application             |
| App 1       |  `0x08008000` | 32 KB | Alternate application          |
| Factory App |  `0x0800C000` | 32 KB | Recovery/factory image         |
| Common API  |  `0x08010000` |     — | Shared bootloader API table    |

The corresponding application addresses are:

```c
#define DEFAULT_APP_ADDRESS   0x08004000
#define APP1_ADDRESS          0x08008000
#define FACTORY_APP_ADDRESS   0x0800C000
```

The important idea is that **each firmware image is independently linked to a fixed Flash address**.

---

# Boot Sequence

## 1. MCU Reset

After reset, the Cortex-M4 obtains two values from the vector table:

```text
Vector Table
+-------------------------------+
| [0] Initial Main Stack Pointer|
| [1] Reset_Handler address     |
| [2] NMI_Handler               |
| [3] HardFault_Handler         |
| ...                           |
+-------------------------------+
```

For the bootloader, this vector table begins at:

```text
0x08000000
```

The first word is the initial MSP.

The second word is the address of `Reset_Handler`.

---

## 2. Bootloader Initialization

The bootloader initializes:

```text
FPU
UART2
SysTick
LED
Push Button
```

The startup sequence is approximately:

```c
fpu_enable();
debug_uart_init();
timebase_init();
led_init();
button_init();
```

---

## 3. Boot Decision

The button is active-low.

```c
if(get_btn_state())
{
    // Enter bootloader menu
}
else
{
    // Start default application
}
```

Therefore:

```text
                 Reset
                   │
                   ▼
          Initialize peripherals
                   │
                   ▼
             Read button
             /          \
            /            \
        pressed         released
           │               │
           ▼               ▼
    UART boot menu      Default App
```

---

# UART Bootloader Menu

When the button is pressed, the bootloader displays:

```text
Bootloader Menu

Available Commands:
1       ==> Run App 1
F       ==> Factory App 2
Any key ==> Run Default App
```

UART2 is configured on:

```text
PA2 → TX
PA3 → RX
```

using:

```text
Alternate Function 7
115200 baud
16 MHz peripheral clock
```

UART reception is interrupt-driven.

---

# UART Receive Flow

When a byte arrives:

```text
USART2
   │
   │ RXNE = 1
   ▼
USART2_IRQHandler()
   │
   ▼
uart_callback()
   │
   ▼
Read USART2->DR
   │
   ▼
Interpret command
```

The command mapping is:

```c
'1'       → APP1
'F' / 'f' → FACTORY_APP
other     → DEFAULT
```

The received command is stored in:

```c
volatile uint8_t g_ui_key;
```

Using `volatile` is important because the variable can be modified inside an interrupt service routine and read from normal application flow.

---

# Application Selection

The bootloader uses an enum:

```c
typedef enum
{
    APP1 = 1,
    FACTORY_APP
} SYS_APPS;
```

Then:

```c
process_btldr_cmds(g_ui_key);
```

selects the corresponding Flash address.

Conceptually:

```text
g_ui_key
   │
   ├── 1 ─────────────► 0x08008000
   │                     App 1
   │
   ├── 2 ─────────────► 0x0800C000
   │                     Factory App
   │
   └── 0 ─────────────► 0x08004000
                         Default App
```

---

# How the Bootloader Jumps to an Application

This is one of the most important parts of the project.

Suppose the bootloader wants to launch:

```text
0x08008000
```

The application vector table is:

```text
0x08008000 → Initial MSP
0x08008004 → Reset_Handler
0x08008008 → NMI_Handler
0x0800800C → HardFault_Handler
...
```

The bootloader performs:

```c
uint32_t app_msp =
    *(uint32_t *)addr_value;

uint32_t app_reset =
    *(uint32_t *)(addr_value + 4);
```

Then:

```c
__set_MSP(app_msp);
```

loads the application's initial stack pointer.

The reset handler is converted into a function pointer:

```c
typedef void (*func_ptr)(void);

func_ptr app_reset_handler;

app_reset_handler = (func_ptr)app_reset;
```

Finally:

```c
app_reset_handler();
```

transfers execution to the application.

---

# What Actually Happens During the Jump?

The transition is:

```text
Bootloader
   │
   │ Read vector table
   │
   ├── [0] → Application MSP
   │
   ├── [1] → Application Reset_Handler
   │
   │
   ├── __set_MSP(application_msp)
   │
   └── branch to Reset_Handler
                    │
                    ▼
              Application
                    │
                    ▼
               SystemInit()
                    │
                    ▼
                main()
```

The bootloader is therefore not calling the application's `main()` directly.

It is entering the application's **startup sequence through `Reset_Handler`**, which is the correct conceptual entry point.

---

# Vector Table Relocation with VTOR

Each application has its own vector table.

Therefore each application relocates the Cortex-M Vector Table Offset Register.

### Default Application

```c
#define VECT_TAB_BASE_ADDRESS FLASH_BASE
#define VECT_TAB_OFFSET      0x4000

SCB->VTOR =
    VECT_TAB_BASE_ADDRESS |
    VECT_TAB_OFFSET;
```

Result:

```text
VTOR = 0x08004000
```

### Application 1

```text
VTOR = 0x08008000
```

### Factory Application

```text
VTOR = 0x0800C000
```

This is essential because interrupts must use the application's vector table rather than the bootloader's.

---

# Why VTOR Still Matters After the Jump

The bootloader branches to the application's `Reset_Handler`, but this is **not the same thing as performing a hardware reset**.

The processor therefore does not automatically reload the vector table from the application's Flash address.

The application explicitly performs:

```c
SCB->VTOR = application_vector_address;
```

This ensures that subsequent interrupts use:

```text
Application Vector Table
```

rather than:

```text
Bootloader Vector Table
```

---

# Shared Bootloader API

One of the more advanced parts of this project is the **common API table**.

Instead of compiling another copy of the bootloader's hardware abstraction code into every application, the bootloader exposes selected functions through a structure of function pointers.

The interface is:

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

The bootloader creates the table:

```c
struct btl_common_apis common_apis
__attribute__((section(".COMMON_APIS"))) =
{
    led_init,
    led_toggle,
    led_on,
    led_off,
    debug_uart_init,
    button_init,
    get_btn_state,
    fpu_enable,
    timebase_init
};
```

The application obtains the table from a fixed Flash address:

```c
struct btl_common_apis *common_apis =
    (struct btl_common_apis *)0x08010000;
```

Then it can call:

```c
common_apis->led_init();
common_apis->debug_uart_init();
common_apis->timebase_init();
```

---

# Why This API Table Matters

This demonstrates an important embedded-systems concept:

## Firmware ABI / Interface Contract

The application does not need to know the implementation of:

```text
led_init()
debug_uart_init()
timebase_init()
fpu_enable()
```

It only needs to know:

```text
where the API table lives
+
the structure layout
+
the function signatures
```

Conceptually:

```text
                 BOOTLOADER
                     │
                     ▼
             Common API Table
                     │
       ┌─────────────┼─────────────┐
       │             │             │
       ▼             ▼             ▼
    LED API       UART API      Timebase API
       │             │             │
       └─────────────┼─────────────┘
                     │
                     ▼
                APPLICATION
```

This is similar in spirit to an interface/ABI layer between separately built firmware components.

---

# Custom Linker Sections

The API table is placed into a custom section:

```c
__attribute__((section(".COMMON_APIS")))
```

The linker script then explicitly places that section at a fixed Flash location.

This demonstrates how the linker can be used to control the exact placement of firmware objects in memory.

The same principle is used for:

```text
.isr_vector
.text
.rodata
.data
.bss
.custom sections
```

---

# Linker Script Learning

Each application has a different Flash origin.

Example:

```ld
FLASH (rx) :
    ORIGIN = 0x08008000,
    LENGTH = 32K
```

This means the linker will treat:

```text
0x08008000
```

as the beginning of that application's address space.

Therefore the application's:

```text
.isr_vector
.text
.rodata
...
```

are all linked relative to the application address.

This is why the same source code can produce multiple independent firmware images simply by changing the linker configuration.

---

# RAM Layout

The project uses:

```text
RAM start = 0x20000000
RAM size  = 128 KB
RAM end   = 0x20020000
```

The linker defines:

```ld
_estack = ORIGIN(RAM) + LENGTH(RAM);
```

Therefore:

```text
_estack = 0x20020000
```

This value becomes the initial stack pointer stored in the application's vector table.

---

# `.data` and `.bss`

The project also demonstrates the standard embedded memory model.

### `.data`

Initialized global/static variables are stored in RAM at runtime but their initial values are stored in Flash.

Therefore:

```ld
.data >RAM AT> FLASH
```

means:

```text
Load Address  → FLASH
Runtime Address → RAM
```

The linker also creates:

```c
_sdata
_edata
_sidata
```

which are used by startup code to copy initialized values from Flash to RAM.

### `.bss`

Uninitialized or zero-initialized global/static variables reside in RAM:

```ld
.bss >RAM
```

The linker defines:

```c
_sbss
_ebss
```

for startup-time zero initialization.

---

# SysTick Timebase

The project implements a simple bare-metal timebase using SysTick.

At a 16 MHz system clock:

```c
#define ONE_MSEC_LOAD 16000
```

The SysTick reload value is:

```c
SysTick->LOAD = ONE_MSEC_LOAD - 1;
```

Therefore one interrupt occurs approximately every:

```text
1 ms
```

The interrupt handler increments:

```c
volatile uint32_t g_curr_tick;
```

through:

```c
static void tick_increment(void)
{
    g_curr_tick += TICK_FREQ;
}
```

The delay function uses elapsed tick subtraction:

```c
while ((get_tick() - tickstart) < wait)
{
}
```

This subtraction-based technique naturally handles unsigned 32-bit tick rollover.

---

# GPIO / BSP

The project uses a simple Board Support Package.

### LED

```text
PA5 → LED
```

Functions:

```c
led_init();
led_toggle();
led_on();
led_off();
```

### Button

```text
PC13 → User Button
```

The button is active-low:

```text
IDR = 1 → Not pressed
IDR = 0 → Pressed
```

---

# FPU Configuration

The STM32F411 contains a Cortex-M4F with a single-precision floating-point unit.

The project explicitly enables access to CP10 and CP11:

```c
SCB->CPACR |= ...
```

This demonstrates how processor-level features can be configured directly through the Cortex-M System Control Block.

---

# UART Implementation

UART2 is configured directly through registers.

Important registers include:

```text
USART2->CR1
USART2->BRR
USART2->SR
USART2->DR
```

The project configures:

```text
Baud       = 115200
TX         = PA2
RX         = PA3
AF         = AF7
```

`printf()` output is routed through:

```c
__io_putchar()
```

which ultimately calls:

```c
uart_write()
```

The transmit function waits for:

```c
SR_TXE
```

before writing to:

```c
USART2->DR
```

---

# Key Embedded Concepts Demonstrated

This project provided hands-on implementation of:

### Cortex-M Startup

- Vector table
- Initial MSP
- Reset Handler
- Startup flow
- `SystemInit()`

### Memory Management

- Flash partitioning
- RAM layout
- Linker scripts
- Section placement
- `.data`
- `.bss`
- Stack
- Heap

### Interrupt Architecture

- NVIC
- USART interrupt
- RXNE
- SysTick interrupt
- Vector table relocation
- VTOR

### C / Embedded C

- Function pointers
- Function-pointer typedefs
- Structures
- `volatile`
- `static`
- Memory-mapped registers
- Bit manipulation
- Custom GCC attributes

### Bootloader Design

- Application selection
- Application validation
- Manual MSP initialization
- Reset handler jump
- Multiple firmware images
- Factory/recovery image
- Shared API interface

---

# Important Learning: `static` Functions

The bootloader uses declarations such as:

```c
static void uart_callback(void);
static void jump_to_app(uint32_t addr);
```

A `static` function at file scope has **internal linkage**.

That means the function is visible only within that translation unit.

This is useful for implementation-private bootloader functions that should not become global symbols.

However, a function pointer or API exposed to another image cannot point to a file-local `static` function from another translation unit in the same way a globally visible symbol can be referenced.

This distinction between:

```text
scope
vs
linkage
```

is an important part of embedded C.

---

# Application Validation

The current implementation includes a basic Flash check:

```c
if (*(uint32_t *)addr_value != 0xFFFFFFFF)
```

This detects whether the first vector-table word is erased Flash.

It is intentionally simple and is useful for learning.

A production bootloader should perform stronger validation, for example:

```text
Check MSP is inside valid RAM
Check Reset_Handler is inside application Flash
Check Thumb bit
Check image length
Check CRC/hash/signature
Check application metadata
Check version
Check compatibility
```

---

# Current Boot Handoff vs Production Bootloader

The current project intentionally keeps the boot process simple so the underlying concepts are visible.

Before a production-quality handoff, the bootloader should additionally consider:

```text
Disable interrupts
Disable SysTick
Clear pending interrupts
Deinitialize bootloader peripherals where required
Relocate VTOR
Use DSB/ISB barriers
Validate MSP
Validate Reset_Handler
Validate image integrity
```

A production firmware-update system would also typically include:

```text
Image metadata
CRC/hash
Versioning
Rollback/recovery
Atomic update strategy
Authentication/signature verification
Power-loss handling
Watchdog integration
```

---

# Important Design Considerations

## 1. Logical image slots vs hardware Flash sectors

The project uses logical 32 KB application regions:

```text
0x08000000
0x08004000
0x08008000
0x0800C000
```

These addresses are convenient for linker placement.

However, **linker regions are not the same thing as physical Flash erase sectors**.

When implementing Flash erase/update functionality, the STM32F4's actual hardware erase-sector geometry must be used.

---

## 2. Common API Address Consistency

The application expects the API table at:

```text
0x08010000
```

Therefore the bootloader linker script must place:

```text
.COMMON_APIS
```

at that exact intended address.

The address used by the linker and the address hard-coded by the applications must always agree.

This is a critical requirement for a fixed-address firmware ABI.

---

# Project Structure

A suggested repository structure is:

```text
STM32F411-Custom-Bootloader/
│
├── README.md
│
├── linker/
│   ├── bootloader.ld
│   ├── default_app.ld
│   ├── app1.ld
│   └── factory_app.ld
│
├── bootloader/
│   ├── main.c
│   ├── bsp.c
│   ├── fpu.c
│   ├── uart.c
│   └── timebase.c
│
├── applications/
│   ├── default_app/
│   │   └── main.c
│   │
│   ├── app1/
│   │   └── main.c
│   │
│   └── factory_app/
│       └── main.c
│
└── docs/
    └── architecture.md
```

---

# Technologies

**Microcontroller**

- STM32F411RETx
- ARM Cortex-M4F

**Programming**

- Embedded C
- ARM Cortex-M programming
- Direct register-level programming

**Peripherals**

- GPIO
- USART2
- SysTick
- NVIC
- FPU

**Toolchain Concepts**

- GCC
- GNU Linker
- Linker scripts
- Custom sections
- Function pointers
- Firmware image layout

---

# What I Learned

The biggest takeaway from this project is that a bootloader is fundamentally a combination of:

```text
CPU startup knowledge
        +
memory/linker knowledge
        +
interrupt/vector-table knowledge
        +
firmware architecture
        +
careful C programming
```

The project moved my understanding from:

> "The MCU starts executing `main()`."

to:

> "The MCU starts from a vector table, obtains the initial stack pointer, enters `Reset_Handler`, initializes the runtime, and then executes application code."

It also demonstrated that separate firmware images can communicate through a carefully designed **fixed-address interface** rather than sharing implementation details.

---

# Future Improvements

Planned extensions for a more complete bootloader include:

- [ ] CRC-based application validation
- [ ] Application metadata/header
- [ ] Firmware version management
- [ ] UART firmware update protocol
- [ ] Flash erase/program implementation
- [ ] Downloaded image verification
- [ ] Watchdog-based recovery
- [ ] Rollback to factory firmware
- [ ] Robust interrupt/peripheral cleanup during handoff
- [ ] Strong MSP and Reset_Handler validation
- [ ] Boot reason detection
- [ ] Application authentication/signature verification
- [ ] Robust shared API versioning
- [ ] Automated build of multiple application images

---

# Key Takeaways

This project demonstrates how to build a custom STM32 boot architecture from the ground up rather than relying entirely on vendor bootloader frameworks.

The most important concepts demonstrated are:

```text
                  STM32 RESET
                       │
                       ▼
                Vector Table
                       │
              ┌────────┴────────┐
              │                 │
          Initial MSP       Reset_Handler
              │                 │
              └────────┬────────┘
                       ▼
                  BOOTLOADER
                       │
          ┌────────────┼────────────┐
          │            │            │
      Button        UART Cmd      Default
          │            │            │
          └────────────┼────────────┘
                       ▼
                Select Image
                       │
                       ▼
                Read App Vector
                       │
               ┌───────┴───────┐
               │               │
              MSP         Reset_Handler
               │               │
               └───────┬───────┘
                       ▼
                APPLICATION
                       │
                       ▼
                  SystemInit()
                       │
                       ▼
                     VTOR
                       │
                       ▼
                    main()
```

---

