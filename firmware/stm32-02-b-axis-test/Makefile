PROJECT := stm32_02_b_axis_test
BUILD_DIR := build
VENDOR := ../vendor/STM32CubeG4

PREFIX := arm-none-eabi-
CC := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE := $(PREFIX)size

CPUFLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
DEFS := -DUSE_HAL_DRIVER -DSTM32G431xx
OPT := -Og

INCLUDES := \
	-IInc \
	-I$(VENDOR)/Drivers/STM32G4xx_HAL_Driver/Inc \
	-I$(VENDOR)/Drivers/STM32G4xx_HAL_Driver/Inc/Legacy \
	-I$(VENDOR)/Drivers/CMSIS/Device/ST/STM32G4xx/Include \
	-I$(VENDOR)/Drivers/CMSIS/Include

CFLAGS := $(CPUFLAGS) $(DEFS) $(INCLUDES) $(OPT) -g3 -Wall -ffunction-sections -fdata-sections
ASFLAGS := $(CPUFLAGS) $(DEFS) $(INCLUDES) -g3
LDSCRIPT := STM32G431RBTX_FLASH.ld
LDFLAGS := $(CPUFLAGS) -T$(LDSCRIPT) --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections -Wl,-Map=$(BUILD_DIR)/$(PROJECT).map
LDLIBS := -Wl,--start-group -lc -lm -Wl,--end-group

C_SOURCES := \
	Src/main.c \
	Src/syscalls.c \
	Src/stm32g4xx_hal_msp.c \
	Src/stm32g4xx_it.c \
	Src/system_stm32g4xx.c \
	$(VENDOR)/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal.c \
	$(VENDOR)/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_cortex.c \
	$(VENDOR)/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_dma.c \
	$(VENDOR)/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_dma_ex.c \
	$(VENDOR)/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_exti.c \
	$(VENDOR)/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash.c \
	$(VENDOR)/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash_ex.c \
	$(VENDOR)/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash_ramfunc.c \
	$(VENDOR)/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_gpio.c \
	$(VENDOR)/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_pwr.c \
	$(VENDOR)/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_pwr_ex.c \
	$(VENDOR)/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_rcc.c \
	$(VENDOR)/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_rcc_ex.c \
	$(VENDOR)/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_tim.c \
	$(VENDOR)/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_tim_ex.c \
	$(VENDOR)/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_uart.c \
	$(VENDOR)/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_uart_ex.c

ASM_SOURCES := Startup/startup_stm32g431rbtx.s

OBJECTS := $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o))) \
	$(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))

vpath %.c $(sort $(dir $(C_SOURCES)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

.PHONY: all clean flash

all: $(BUILD_DIR)/$(PROJECT).elf $(BUILD_DIR)/$(PROJECT).bin

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.s | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/$(PROJECT).elf: $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@
	$(SIZE) $@

$(BUILD_DIR)/$(PROJECT).bin: $(BUILD_DIR)/$(PROJECT).elf
	$(OBJCOPY) -O binary $< $@

flash: $(BUILD_DIR)/$(PROJECT).elf
	openocd -f interface/stlink.cfg -f target/stm32g4x.cfg -c "program $(abspath $(BUILD_DIR)/$(PROJECT).elf) verify reset exit"

clean:
	rm -rf $(BUILD_DIR)
