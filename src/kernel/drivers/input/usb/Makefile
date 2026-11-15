# USBX host core and initial class drivers. Hardware HCDs are deliberately
# excluded until their OS/DMA/standalone contracts are implemented.
USBX_ROOT := src/kernel/drivers/input/usb/usbx
USBX_PORT := src/kernel/drivers/input/usb/opensillicon
USBX_CORE_SOURCES := $(wildcard $(USBX_ROOT)/common/core/src/ux_system*.c) \
    $(wildcard $(USBX_ROOT)/common/core/src/ux_utility*.c) \
    $(wildcard $(USBX_ROOT)/common/core/src/ux_host_stack*.c)
USBX_UNUSED_UTILITIES := pci_read pci_write physical_address virtual_address \
    set_interrupt_handler
USBX_CORE_SOURCES := $(filter-out $(foreach util,$(USBX_UNUSED_UTILITIES),\
    $(USBX_ROOT)/common/core/src/ux_utility_$(util).c),$(USBX_CORE_SOURCES))
USBX_CLASS_SOURCES := $(foreach class,hub hid storage,\
    $(wildcard $(USBX_ROOT)/common/usbx_host_classes/src/ux_host_class_$(class)*.c))
USBX_SOURCES := $(filter-out %_thread.c,$(USBX_CORE_SOURCES) $(USBX_CLASS_SOURCES))
USBX_OBJECTS := $(patsubst $(USBX_ROOT)/%.c,$(BUILD_DIR)/usbx/%.o,$(USBX_SOURCES))
USBX_CFLAGS := $(KERNEL_CFLAGS) -fno-builtin -DUX_INCLUDE_USER_DEFINE_FILE \
    -I$(USBX_PORT) -I$(USBX_ROOT)/common/core/inc \
    -I$(USBX_ROOT)/common/usbx_host_classes/inc \
    -Wno-unused-parameter -Wno-unused-but-set-variable -Wno-unused-but-set-parameter -Wno-parentheses
$(BUILD_DIR)/usbx/%.o: $(USBX_ROOT)/%.c $(USBX_PORT)/ux_port.h $(USBX_PORT)/ux_user.h
	mkdir -p $(@D)
	$(CC) $(USBX_CFLAGS) -c $< -o $@
$(BUILD_DIR)/usbx/host.o: $(USBX_PORT)/host.c | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(USBX_CFLAGS) -c $< -o $@

.PHONY: usbx
usbx: $(BUILD_DIR)/usbx/libusbx.a
$(BUILD_DIR)/usbx/libusbx.a: $(USBX_OBJECTS)
	$(AR) rcs $@ $^
$(BUILD_DIR)/usbx/xhci.o: $(USBX_PORT)/xhci.c | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(USBX_CFLAGS) -c $< -o $@

USBX_KERNEL_OBJECTS := $(BUILD_DIR)/usbx/host.o $(BUILD_DIR)/usbx/xhci.o
-include $(USBX_OBJECTS:.o=.d) $(USBX_KERNEL_OBJECTS:.o=.d)
