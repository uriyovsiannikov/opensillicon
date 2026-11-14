#include "arch/x86_64/pci_resource.h"
#include "kernel/driver_api.h"
#include "kernel/log.h"
#include "kernel/timer.h"

/* Intel CNP I219-V (7), as identified by FreeBSD's e1000_hw.h. */
#define I219_VENDOR 0x8086U
#define I219_DEVICE 0x15bcU
#define I219_STATUS 0x0008U
#define I219_RAL0   0x5400U
#define I219_RAH0   0x5404U
#define I219_FWSM   0x5b54U
#define I219_RAH_VALID (1U << 31)
#define I219_MDIC 0x0020U
#define I219_EXTCNF_CTRL 0x0f00U
#define I219_SWFLAG (1U << 5)
#define I219_MDIC_READ (1U << 27)
#define I219_MDIC_READY (1U << 28)
#define I219_MDIC_ERROR (1U << 30)
#define I219_PHY_ADDRESS 2U
#define I219_WAIT_TICKS 10U


struct probe_result {
	size_t matches;
	struct pci_device_info pci;
};

static bool
find_controller(const struct pci_device_info *pci, void *context)
{
	struct probe_result *result = context;
	if (pci->vendor_id == I219_VENDOR && pci->device_id == I219_DEVICE &&
	    (pci->header_type & 0x7fU) == 0 && pci->class_code == 2 &&
	    pci->subclass == 0 && pci->programming_interface == 0) {
		result->pci = *pci;
		result->matches++;
	}
	return true;
}

static void
log_hex(uint32_t value, unsigned int digits)
{
	static const char alphabet[] = "0123456789abcdef";
	char text[9];
	for (unsigned int i = digits; i != 0; i--) {
		text[i - 1] = alphabet[value & 15U];
		value >>= 4;
	}
	text[digits] = '\0';
	klog(text);
}

static void
report(const volatile uint32_t *registers)
{
	uint32_t status = registers[I219_STATUS / 4];
	uint32_t fwsm = registers[I219_FWSM / 4];
	uint32_t low = registers[I219_RAL0 / 4];
	uint32_t high = registers[I219_RAH0 / 4];

	if (status == UINT32_MAX || fwsm == UINT32_MAX) {
		klog("i219: controller registers did not respond\n");
		return;
	}
	klog("i219: STATUS=0x");
	log_hex(status, 8);
	klog(" FWSM=0x");
	log_hex(fwsm, 8);
	klog((status & (1U << 1)) ? " link=up\n" : " link=down\n");

	if (!(high & I219_RAH_VALID) || (low & 1U) ||
	    (low == 0 && (high & 0xffffU) == 0)) {
		klog("i219: no valid MAC in receive address register 0\n");
		return;
	}
	klog("i219: MAC=");
	for (unsigned int i = 0; i < 6; i++) {
		if (i != 0)
			klog(":");
		log_hex(i < 4 ? low >> (8 * i) : high >> (8 * (i - 4)), 2);
	}
	klog("\n");
}

/* The CNP PHY exposes its basic Clause 22 registers at address 2.
 * Follow the ICH8/PCH software ownership protocol before using MDIC. */
static bool
wait_bits(const volatile uint32_t *reg, uint32_t mask, uint32_t expected,
    uint32_t *value)
{
	uint64_t start = timer_jiffies();
	do {
		*value = *reg;
		if (*value == UINT32_MAX)
			return false;
		if ((*value & mask) == expected)
			return true;
		timer_wait_for_interrupt();
	} while (timer_jiffies() - start < I219_WAIT_TICKS);
	return false;
}

static bool
phy_read(volatile uint32_t *registers, unsigned int reg, uint16_t *value)
{
	volatile uint32_t *mdic = &registers[I219_MDIC / 4];
	uint32_t command = (I219_PHY_ADDRESS << 21) | (reg << 16);
	uint32_t reply;
	*mdic = I219_MDIC_READ | command;
	if (!wait_bits(mdic, I219_MDIC_READY, I219_MDIC_READY, &reply) ||
	    (reply & I219_MDIC_ERROR) || ((reply >> 16) & 31U) != reg)
		return false;
	*value = (uint16_t)reply;
	return true;
}

static void
report_phy(volatile uint32_t *registers)
{
	volatile uint32_t *control = &registers[I219_EXTCNF_CTRL / 4];
	uint32_t config;
	uint16_t id_high, id_low, basic_status;

	if (!wait_bits(control, I219_SWFLAG, 0, &config)) {
		klog("i219: PHY ownership busy or unavailable\n");
		return;
	}
	*control = config | I219_SWFLAG;
	if (!wait_bits(control, I219_SWFLAG, I219_SWFLAG, &config)) {
		klog("i219: could not acquire PHY ownership\n");
		return;
	}

	/* BMSR link status is latch-low: use the second read. */
	bool ok = phy_read(registers, 2, &id_high) &&
	    phy_read(registers, 3, &id_low) &&
	    phy_read(registers, 1, &basic_status) &&
	    phy_read(registers, 1, &basic_status);
	config = *control;
	if (config != UINT32_MAX && (config & I219_SWFLAG)) {
		*control = config & ~I219_SWFLAG;
		config = *control;
	}
	if (config == UINT32_MAX || (config & I219_SWFLAG))
		klog("i219: could not verify release of PHY ownership\n");

	if (!ok || id_high == 0 || id_high == UINT16_MAX ||
	    basic_status == UINT16_MAX) {
		klog("i219: PHY register read failed or invalid PHY ID\n");
		return;
	}
	klog("i219: PHY ID=0x");
	log_hex(id_high, 4);
	log_hex(id_low, 4);
	klog(" BMSR=0x");
	log_hex(basic_status, 4);
	klog((basic_status & 4U) ? " PHY link=up\n" : " PHY link=down\n");
}

int
module_init(void)
{
	struct probe_result result = {0};
	if (kernel_driver_api_version() != KERNEL_DRIVER_API_VERSION ||
	    !pci_enumerate(find_controller, &result))
		return -1;
	if (result.matches != 1) {
		klog(result.matches ? "i219: multiple matching controllers\n" :
		    "i219: no matching 8086:15bc controller\n");
		return 0;
	}
	klog("i219: detected Intel I219-V (7) [8086:15bc]\n");

	struct pci_device_lease device;
	if (!(result.pci.command & 2U)) {
		klog("i219: firmware left PCI memory decoding disabled\n");
		return 0;
	}
	if (!pci_device_claim(&result.pci, &device)) {
		klog("i219: could not claim PCI function\n");
		return 0;
	}
	struct pci_bar_resource bar;
	struct pci_mmio_lease mapping;
	if (!pci_bar_probe_size(&device, 0, &bar) ||
	    !pci_mmio_map(&device, &bar, 0, I219_FWSM + 4, &mapping)) {
		klog("i219: could not validate and map BAR0\n");
	} else {
		volatile void *address;
		if (pci_mmio_lease_address(&mapping, &address)) {
			report(address);
			report_phy(address);
		} else
			klog("i219: could not access BAR0 mapping\n");
		if (!pci_mmio_release(&mapping))
			klog("i219: could not release BAR0 mapping\n");
	}
	if (!pci_device_release(&device))
		klog("i219: could not release PCI function\n");
	return 0;
}
