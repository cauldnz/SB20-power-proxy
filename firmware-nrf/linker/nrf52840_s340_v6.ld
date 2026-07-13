/* Linker script for the S340 SoftDevice (concurrent ANT+BLE) on the nRF52840.
 *
 * Based on the Adafruit core's nrf52840_s140_v7.ld — ONLY the FLASH region changes: the app sits
 * above MBR (0x1000) + the S340 SoftDevice (0x30000) = application FLASH ORIGIN 0x31000 (vs S140-v7's
 * 0x27000). Verified from the S340 6.1.1 headers: MBR_SIZE 0x1000 (nrf_mbr.h), SD_FLASH_SIZE 0x30000
 * (nrf_sdm.h). The RAM base is runtime-negotiated by sd_ble_enable()/sd_ant_enable() — 0x20006000 is
 * the S140 starting value; if concurrent BLE+ANT needs more, the enable call returns the required base
 * (bump this ORIGIN on-board per that value — a hardware-only discovery, see nrf-roadmap.md P3).
 */
SEARCH_DIR(.)
GROUP(-lgcc -lc -lnosys)

MEMORY
{
  FLASH (rx)     : ORIGIN = 0x31000, LENGTH = 0xED000 - 0x31000

  RAM (rwx) :  ORIGIN = 0x20006000, LENGTH = 0x20040000 - 0x20006000
}

SECTIONS
{
  . = ALIGN(4);
  .svc_data :
  {
    PROVIDE(__start_svc_data = .);
    KEEP(*(.svc_data))
    PROVIDE(__stop_svc_data = .);
  } > RAM

  .fs_data :
  {
    PROVIDE(__start_fs_data = .);
    KEEP(*(.fs_data))
    PROVIDE(__stop_fs_data = .);
  } > RAM
} INSERT AFTER .data;

INCLUDE "nrf52_common.ld"
