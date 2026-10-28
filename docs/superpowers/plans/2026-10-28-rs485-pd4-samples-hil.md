# RS-485 GPIO DE on PD4 for RTU samples + HIL validation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (native execution).

**Goal:** Switch `samples/server_rtu` and `samples/client_rtu` from hardware DE (or no DE) to GPIO-controlled RE/DE on PD4, build both samples, and validate RS-485 communication on physical Nucleo-H743ZI hardware.

**Architecture:** The Zephyr serial transport already supports `de-gpios` software DE. We remove the USART2 RTS/DE hardware path and configure PD4 as a normal GPIO output for DE. `de-mode = "auto"` then falls back to GPIO DE. The same pin acts as RE/DE because the transceiver ties RE (active-low) and DE (active-high) together: DE high = TX, DE low = RX.

**Tech Stack:** Zephyr RTOS, Devicetree, STM32H743ZI (Nucleo-H743ZI), RS-485 transceiver, USB-RS485 adapter.

**Spec:** Derived from `AGENTS.md` hardware rules and the existing `dts/bindings/serial/nanomodbus,serial.yaml` binding.

## Global Constraints
- Keep `nanomodbus.c/h` untouched; all changes are in `zephyr/` and `samples/`.
- Use Zephyr logging only (`LOG_INF`/`LOG_ERR`); no `printf`/`printk`.
- Do not modify linker scripts, startup code, or CI pipelines.
- Physical flashing requires explicit human approval per governance rules.
- Build command for out-of-tree module: `-- -DEXTRA_ZEPHYR_MODULES=$PWD`.

## Review Focus
- PD4 is freed from USART RTS before being reclaimed as GPIO.
- `de-mode = "auto"` resolves to GPIO DE when no `UART_CFG_FLOW_CTRL_RS485` is present.
- DMA RX still works after removing hardware DE; if not, sample falls back to IRQ.
- HIL distinguishes bus A/B polarity for the RS-485 transceiver.
- Two samples can coexist on the same bus without address collisions.

---

### Task 1: Update `samples/server_rtu` to GPIO DE on PD4

**Files:**
- Modify: `samples/server_rtu/boards/nucleo_h743zi.overlay`
- Modify: `samples/server_rtu/boards/nucleo_h743zi.conf` (if needed)

**Interfaces:**
- Consumes: `NMBS_Z_SERIAL_TRANSPORT_CONFIG_DT(MODBUS_NODE)` macro.
- Produces: Devicetree node `modbus0` with `de-gpios = <&gpiod 4 GPIO_ACTIVE_HIGH>`.

- [x] **Step 1: Remove hardware DE pinctrl and properties**
  - Drop `&usart2_rts_pd4` from `pinctrl-0`.
  - Drop `de-enable`, `de-assert-time`, `de-deassert-time`.
  - Keep `&usart2_tx_pd5 &usart2_rx_pd6`.

- [x] **Step 2: Add GPIO DE property**
  - Inside `modbus0: modbus0 { ... }` add:
    ```dts
    de-gpios = <&gpiod 4 GPIO_ACTIVE_HIGH>;
    ```
  - Update overlay comments to explain PD4 is now software RE/DE.

- [x] **Step 3: Verify sample still builds**
  Run:
  ```sh
  west build -p always -b nucleo_h743zi samples/server_rtu -d build_server_rtu -- -DEXTRA_ZEPHYR_MODULES=$PWD
  ```
  Expected: build succeeds.

- [x] **Step 4: Commit**
  ```sh
  git add samples/server_rtu/boards/nucleo_h743zi.overlay
  git commit -m "samples/server_rtu: use GPIO RE/DE on PD4"
  ```

### Task 2: Update `samples/client_rtu` to GPIO DE on PD4

**Files:**
- Modify: `samples/client_rtu/boards/nucleo_h743zi.overlay`

**Interfaces:**
- Consumes: same `NMBS_Z_SERIAL_TRANSPORT_CONFIG_DT` macro.
- Produces: `modbus0` node with GPIO DE.

- [x] **Step 1: Add GPIO DE and update comments**
  - Add `de-gpios = <&gpiod 4 GPIO_ACTIVE_HIGH>;` inside `modbus0`.
  - Update overlay comments to state PD4 drives RE/DE.

- [x] **Step 2: Verify build**
  Run:
  ```sh
  west build -p always -b nucleo_h743zi samples/client_rtu -d build_client_rtu -- -DEXTRA_ZEPHYR_MODULES=$PWD
  ```
  Expected: build succeeds.

- [x] **Step 3: Commit**
  ```sh
  git add samples/client_rtu/boards/nucleo_h743zi.overlay
  git commit -m "samples/client_rtu: add GPIO RE/DE on PD4"
  ```

### Task 3: HIL validation on physical hardware

**Files:**
- None (uses build artifacts from Task 1 and Task 2).

**Interfaces:**
- Consumes: `build_server_rtu/zephyr/zephyr.elf` and `build_client_rtu/zephyr/zephyr.elf`.
- Produces: HIL test report confirming Modbus RTU request/response exchange.

- [x] **Step 1: Confirm hardware setup with human**
  - A single Nucleo-H743ZI was available, so the counterpart was simulated from
    the PC instead of using a second board.
  - Board: `server_rtu` (slave address 1) or `client_rtu`, PD4 RE/DE.
  - PD5 → DI, PD6 → RO, PD4 → DE with RE tied to DE; A/B straight; common GND.
  - Console/shell on `/dev/ttyACM0` (ST-LINK VCP, USART3).
  - Modbus counterpart on `/dev/ttyUSB0` (USB-RS485 adapter).

- [x] **Step 2: Obtain explicit human approval to flash**
  Approval was granted per board and per iteration.

- [x] **Step 3: Flash `server_rtu`**
  `agentic-hil` had no project configuration and could not attach (normal
  ST-LINK attach fails with `DEV_TARGET_NOT_HALTED`), so flashing used
  `STM32_Programmer_CLI -c port=SWD mode=UR freq=100 -w <elf> -v -rst`
  (connect-under-reset). Expected: "Modbus RTU Server started".

- [x] **Step 4: Flash `client_rtu`**
  Same procedure. Expected: the client polls FC03/FC06.

- [x] **Step 5: Verify RS-485 frame exchange**
  - `nmbs status -s srv0` reports `Frames Received`, `Frames Transmitted` and
    `Requests Handled` incrementing while the PC counterpart polls.
  - `nmbs list` enumerates the registered RTU instance on both samples.
  - Final bidirectional validation was performed by the human partner with an
    external Modbus tool at 115200 bps on both samples.

- [x] **Step 6: Report result**
  Both samples build and exchange Modbus RTU frames on hardware at 115200 with
  an FTDI USB-RS485 adapter. Findings recorded in `docs/known_limitations.md`
  §5.3: a CH340/CH341 adapter cannot complete a frame at 115200 because its
  ~1 ms USB framing exceeds the 750 µs `t1.5` resync budget.

