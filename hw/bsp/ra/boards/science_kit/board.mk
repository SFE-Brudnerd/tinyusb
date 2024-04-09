CPU_CORE = cortex-m23
MCU_VARIANT = ra2a1

LD_FILE = ${BOARD_PATH}/${BOARD}.ld

# For flash-jlink target
JLINK_DEVICE = R7FA4M1AB

USB_FIRST_BULK_PIPE=4
USB_NO_ISOCRONOUS_PIPE=1
USB_NUMBER_OF_PIPES=8

flash: flash-jlink