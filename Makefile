# ------------------------------------------------
#
# @file Makefile
# @brief N32WB452 RT-Thread（仓库根目录唯一 Makefile）
#
# ------------------------------------------------

TARGET = User

JOBS ?= 12
ifeq ($(MAKECMDGOALS),)
MAKEFLAGS += -j$(JOBS)
else ifneq ($(filter all,$(MAKECMDGOALS)),)
MAKEFLAGS += -j$(JOBS)
endif

JLINK_PORT     ?= J12
JLINK_IF       ?= SWD
JLINK_DEVICE   ?= N32WB452CE
JLINK_SPEED    ?= 1000
JLINK_SELECT   ?= USB

ifeq ($(release), y)
DEBUG = 0
else
DEBUG = 1
endif
OPT = -O0

ROOT      := $(abspath .)
TOOLS_DIR := $(ROOT)/tools
BUILD_DIR := $(ROOT)/build

CFG_DIR   := $(ROOT)/config
APP_DIR   := $(ROOT)/app
SVC_DIR   := $(ROOT)/services
BOARD_DIR := $(ROOT)/board
BSP_DIR   := $(ROOT)/BSP

FW_DIR  := $(ROOT)/firmware
RTT_DIR := $(ROOT)/middlewares/rt-thread
DRV_DIR := $(ROOT)/DeviceDrivers

TARGET_PLATFORM := n32wb452
DEFS += -DN32WB452
DEFS += -DUSE_STDPERIPH_DRIVER
DEFS += -DRT_USING_NEWLIB
DEFS += -DHAVE_SIGVAL -DHAVE_SIGEVENT -DHAVE_SIGINFO
DEFS += -DHAVE_SYS_SELECT_H
DEFS += -DHSE_VALUE=32000000

CROSS_COMPILE ?= arm-none-eabi-

######################################
# Sources
######################################
C_SOURCES += $(APP_DIR)/main.c
C_SOURCES += $(SVC_DIR)/usb/usb_app.c
C_SOURCES += $(SVC_DIR)/usb/cdc_acm.c
C_SOURCES += $(SVC_DIR)/log/ulog_cdc_be.c
C_SOURCES += $(SVC_DIR)/stream/stream.c
C_SOURCES += $(SVC_DIR)/cli/cli.c
C_SOURCES += $(SVC_DIR)/cli/cli_json.c
C_SOURCES += $(SVC_DIR)/cli/cli_test.c
C_SOURCES += $(SVC_DIR)/cli/cli_io.c
C_SOURCES += $(SVC_DIR)/cfg/cfg.c

C_SOURCES += $(CFG_DIR)/product_config.c

C_SOURCES += $(BOARD_DIR)/board.c
C_SOURCES += $(BOARD_DIR)/board_clock.c
C_SOURCES += $(BOARD_DIR)/usb_hw.c
C_SOURCES += $(BOARD_DIR)/n32wb452_it.c

C_SOURCES += $(BSP_DIR)/pwr/pwr_plna.c
C_SOURCES += $(BSP_DIR)/pm/pm_stop0.c
C_SOURCES += $(BSP_DIR)/led/led.c
C_SOURCES += $(BSP_DIR)/adc/adc_bat.c
C_SOURCES += $(BSP_DIR)/gnss/gnss.c
C_SOURCES += $(BSP_DIR)/rdss/rdss.c
C_SOURCES += $(BSP_DIR)/rtc/rtc_hw.c
C_SOURCES += $(APP_DIR)/mode/mode.c
C_SOURCES += $(APP_DIR)/session/session_alarm.c
C_SOURCES += $(APP_DIR)/session/msg_pack.c
C_SOURCES += $(BSP_DIR)/key/key.c
C_SOURCES += $(BSP_DIR)/key/board_key_irq.c

C_SOURCES += $(FW_DIR)/CMSIS/device/system_n32wb452.c
C_SOURCES += $(FW_DIR)/$(TARGET_PLATFORM)_std_periph_driver/src/n32wb452_gpio.c
C_SOURCES += $(FW_DIR)/$(TARGET_PLATFORM)_std_periph_driver/src/n32wb452_rcc.c
C_SOURCES += $(FW_DIR)/$(TARGET_PLATFORM)_std_periph_driver/src/n32wb452_exti.c
C_SOURCES += $(FW_DIR)/$(TARGET_PLATFORM)_std_periph_driver/src/n32wb452_usart.c
C_SOURCES += $(FW_DIR)/$(TARGET_PLATFORM)_std_periph_driver/src/n32wb452_adc.c
C_SOURCES += $(FW_DIR)/$(TARGET_PLATFORM)_std_periph_driver/src/n32wb452_rtc.c
C_SOURCES += $(FW_DIR)/$(TARGET_PLATFORM)_std_periph_driver/src/n32wb452_pwr.c
C_SOURCES += $(FW_DIR)/$(TARGET_PLATFORM)_std_periph_driver/src/n32wb452_bkp.c
C_SOURCES += $(FW_DIR)/$(TARGET_PLATFORM)_std_periph_driver/src/misc.c
C_SOURCES += $(FW_DIR)/$(TARGET_PLATFORM)_std_periph_driver/src/n32wb452_iwdg.c

C_SOURCES += $(RTT_DIR)/src/clock.c
C_SOURCES += $(RTT_DIR)/src/device.c
C_SOURCES += $(RTT_DIR)/src/idle.c
C_SOURCES += $(RTT_DIR)/src/ipc.c
C_SOURCES += $(RTT_DIR)/src/irq.c
C_SOURCES += $(RTT_DIR)/src/kservice.c
C_SOURCES += $(RTT_DIR)/src/mem.c
C_SOURCES += $(RTT_DIR)/src/memheap.c
C_SOURCES += $(RTT_DIR)/src/mempool.c
C_SOURCES += $(RTT_DIR)/src/object.c
C_SOURCES += $(RTT_DIR)/src/scheduler.c
C_SOURCES += $(RTT_DIR)/src/slab.c
C_SOURCES += $(RTT_DIR)/src/thread.c
C_SOURCES += $(RTT_DIR)/src/timer.c
C_SOURCES += $(RTT_DIR)/src/components.c
C_SOURCES += $(RTT_DIR)/src/cpu.c
C_SOURCES += $(RTT_DIR)/src/signal.c
C_SOURCES += $(RTT_DIR)/libcpu/cortex-m4/cpuport.c

C_SOURCES += $(DRV_DIR)/gpio/src/drv_gpio.c
C_SOURCES += $(DRV_DIR)/uart/src/drv_usart.c
C_SOURCES += $(DRV_DIR)/watchdog/src/drv_wdt.c

C_SOURCES += $(RTT_DIR)/components/drivers/misc/pin.c
C_SOURCES += $(RTT_DIR)/components/drivers/serial/serial.c
C_SOURCES += $(RTT_DIR)/components/drivers/src/completion.c
C_SOURCES += $(RTT_DIR)/components/drivers/src/ringblk_buf.c
C_SOURCES += $(RTT_DIR)/components/drivers/watchdog/watchdog.c

C_SOURCES += $(RTT_DIR)/components/utilities/ulog/ulog.c

C_SOURCES += $(RTT_DIR)/components/drivers/cherryusb/class/cdc/usbd_cdc.c
C_SOURCES += $(RTT_DIR)/components/drivers/cherryusb/core/usbd_core.c
C_SOURCES += $(RTT_DIR)/components/drivers/cherryusb/port/fsdev/usb_dc_fsdev.c

ASM_SOURCES_S += $(FW_DIR)/CMSIS/device/startup/startup_$(TARGET_PLATFORM)_gcc.s
ASM_SOURCES_S_UPPER += $(RTT_DIR)/libcpu/cortex-m4/context_gcc.S

######################################
# Includes
######################################
C_INCLUDES += -I$(CFG_DIR)
C_INCLUDES += -I$(APP_DIR)
C_INCLUDES += -I$(APP_DIR)/mode
C_INCLUDES += -I$(APP_DIR)/session
C_INCLUDES += -I$(SVC_DIR)/cli
C_INCLUDES += -I$(SVC_DIR)/stream
C_INCLUDES += -I$(SVC_DIR)/cfg
C_INCLUDES += -I$(SVC_DIR)/log
C_INCLUDES += -I$(SVC_DIR)/usb
C_INCLUDES += -I$(BOARD_DIR)
C_INCLUDES += -I$(BSP_DIR)/key
C_INCLUDES += -I$(BSP_DIR)/adc
C_INCLUDES += -I$(BSP_DIR)/led
C_INCLUDES += -I$(BSP_DIR)/pwr
C_INCLUDES += -I$(BSP_DIR)/pm
C_INCLUDES += -I$(BSP_DIR)/rdss
C_INCLUDES += -I$(BSP_DIR)/gnss
C_INCLUDES += -I$(BSP_DIR)/rtc
C_INCLUDES += -I$(FW_DIR)/CMSIS/core
C_INCLUDES += -I$(FW_DIR)/CMSIS/device
C_INCLUDES += -I$(FW_DIR)/$(TARGET_PLATFORM)_std_periph_driver/inc
C_INCLUDES += -I$(RTT_DIR)/include
C_INCLUDES += -I$(RTT_DIR)/include/libc
C_INCLUDES += -I$(DRV_DIR)/inc
C_INCLUDES += -I$(DRV_DIR)/gpio/inc
C_INCLUDES += -I$(DRV_DIR)/uart/inc
C_INCLUDES += -I$(DRV_DIR)/watchdog/inc
C_INCLUDES += -I$(RTT_DIR)/components/drivers/include
C_INCLUDES += -I$(RTT_DIR)/components/drivers/include/drivers
C_INCLUDES += -I$(RTT_DIR)/components/drivers/include/ipc
C_INCLUDES += -I$(RTT_DIR)/components/utilities/ulog
C_INCLUDES += -I$(RTT_DIR)/components/drivers/cherryusb/core
C_INCLUDES += -I$(RTT_DIR)/components/drivers/cherryusb/common
C_INCLUDES += -I$(RTT_DIR)/components/drivers/cherryusb/class/cdc
C_INCLUDES += -I$(RTT_DIR)/components/drivers/cherryusb/port/fsdev

CPU = -mcpu=cortex-m4
FPU = -mfpu=fpv4-sp-d16
FLOAT-ABI = -mfloat-abi=softfp
MCU = $(CPU) -mthumb $(FPU) $(FLOAT-ABI)
ASM_IT = -Wa,-mimplicit-it=thumb

CFLAGS += $(MCU) -Wall $(OPT)
CFLAGS += -ffunction-sections -fdata-sections
CFLAGS += -fno-common -fmessage-length=0
ifeq ($(DEBUG), 1)
CFLAGS += -g -gdwarf-2
endif
CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"

ASFLAGS += $(MCU) -Wall $(OPT) $(ASM_IT)
ifeq ($(DEBUG), 1)
ASFLAGS += -g -gdwarf-2
endif

LFLAGS += $(MCU)
LFLAGS += -Wl,--gc-sections
LFLAGS += --specs=nano.specs --specs=nosys.specs
LFLAGS += -lc -lm -lnosys
LFLAGS += -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref
LDSCRIPT = $(FW_DIR)/CMSIS/device/$(TARGET_PLATFORM)_flash.ld

OBJECTS  = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES_S:.s=.o)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES_S_UPPER:.S=.o)))

vpath %.c $(sort $(dir $(C_SOURCES)))
vpath %.s $(sort $(dir $(ASM_SOURCES_S)))
vpath %.S $(sort $(dir $(ASM_SOURCES_S_UPPER)))

.PHONY: all clean size download flash reset info

all: info $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin size

info:
	@echo "Project root  : $(ROOT)"
	@echo "Build dir     : $(BUILD_DIR)"
	@echo "Download port : $(JLINK_PORT) ($(JLINK_IF)/J-Link)"
	@echo "Device        : $(JLINK_DEVICE)  speed=$(JLINK_SPEED) kHz"
	@echo "Parallel jobs : $(JOBS) (only for make / make all)"
	@echo "Targets       : make reset | make flash"

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	$(CROSS_COMPILE)gcc $(CFLAGS) $(DEFS) $(C_INCLUDES) -c \
		-Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)
	$(CROSS_COMPILE)gcc -x assembler-with-cpp $(ASFLAGS) $(DEFS) $(C_INCLUDES) -c $< -o $@

$(BUILD_DIR)/%.o: %.S Makefile | $(BUILD_DIR)
	$(CROSS_COMPILE)gcc -x assembler-with-cpp $(ASFLAGS) $(DEFS) $(C_INCLUDES) -c $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	$(CROSS_COMPILE)gcc $(OBJECTS) $(LFLAGS) -T$(LDSCRIPT) -o $@

$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	$(CROSS_COMPILE)objcopy -O binary -S $< $@

$(BUILD_DIR)/$(TARGET).hex: $(BUILD_DIR)/$(TARGET).elf
	$(CROSS_COMPILE)objcopy -O ihex -S $< $@

$(BUILD_DIR):
	@mkdir -p $@

size: $(BUILD_DIR)/$(TARGET).elf
	$(CROSS_COMPILE)size $<

clean:
	@rm -rf $(BUILD_DIR)

-include $(wildcard $(BUILD_DIR)/*.d)

JLINK_RESET_SCRIPT := $(TOOLS_DIR)/jlink/reset.jlink
JLINK_FLASH_SCRIPT := $(TOOLS_DIR)/jlink/flash.jlink
JLINK_EXE          ?= $(shell command -v JLinkExe 2>/dev/null || echo /opt/SEGGER/JLink/JLinkExe)

define jlink_cmd
	@if ! command -v "$(JLINK_EXE)" >/dev/null 2>&1 && [ ! -x "$(JLINK_EXE)" ]; then \
		echo "找不到 J-Link: $(JLINK_EXE)"; \
		echo "请安装 SEGGER J-Link，或: make flash JLINK_EXE=/绝对路径/JLinkExe"; \
		exit 1; \
	fi
	@echo "[$(JLINK_PORT)] J-Link $(JLINK_IF) -> $(JLINK_DEVICE)"
	cd "$(ROOT)" && "$(JLINK_EXE)" -select $(JLINK_SELECT) -device $(JLINK_DEVICE) \
		-if $(JLINK_IF) -speed $(JLINK_SPEED) -autoconnect 1 \
		-CommanderScript "$(1)"
endef

reset:
	$(call jlink_cmd,$(JLINK_RESET_SCRIPT))
	@echo "MCU reset (halt) via $(JLINK_PORT)"

flash: $(BUILD_DIR)/$(TARGET).hex
	$(call jlink_cmd,$(JLINK_FLASH_SCRIPT))
	@echo "Flashed build/$(TARGET).hex via $(JLINK_PORT) (reset + run)"

download: flash
