#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "kernel/sysinfo.h"
#include "kernel/errno.h"
#include "kernel/module.h"
#include "kernel/block.h"
#include "kernel/smp.h"
#include "kernel/usb_host.h"
#include "arch/x86_64/pci.h"

static char boot_arguments[1024];
void
kernel_info_set_bootargs(const char *arguments)
{
	size_t length = 0;
	while (arguments != NULL && length < sizeof(boot_arguments) - 1 &&
	    arguments[length] != 0)
		length++;
	if (length)
		memcpy(boot_arguments, arguments, length);
	boot_arguments[length] = 0;
}
const char *
kernel_info_bootargs(void)
{
	return boot_arguments;
}

static bool
append_text(struct kernel_info_report *node, const char *text)
{
	size_t length = strlen(text);

	if (length > sizeof(node->content) - 1 - node->content_length)
		return false;
	memcpy(node->content + node->content_length, text, length);
	node->content_length += length;
	return true;
}

static bool
append_u64(struct kernel_info_report *node, uint64_t value)
{
	char digits[20];
	size_t length = 0;

	do {
		digits[length++] = (char)('0' + value % 10U);
		value /= 10U;
	} while (value != 0);
	if (length > sizeof(node->content) - 1 - node->content_length)
		return false;
	while (length != 0)
		node->content[node->content_length++] = digits[--length];
	return true;
}

static bool
append_hex(struct kernel_info_report *node, uint64_t value, unsigned int digits)
{
	static const char hex[] = "0123456789abcdef";
	if (digits > sizeof(node->content) - 1 - node->content_length)
		return false;
	for (unsigned int i = digits; i != 0; i--)
		node->content[node->content_length++] =
		    hex[(value >> ((i - 1U) * 4U)) & 15U];
	return true;
}

static bool
info_pci_identity(
    const struct pci_device_info *device, struct kernel_info_report *node)
{
	return append_hex(node, device->segment, 4) && append_text(node, ":") &&
	       append_hex(node, device->bus, 2) && append_text(node, ":") &&
	       append_hex(node, device->device, 2) && append_text(node, ".") &&
	       append_u64(node, device->function) &&
	       append_text(node, " class=") &&
	       append_hex(node, device->class_code, 2) &&
	       append_hex(node, device->subclass, 2) &&
	       append_hex(node, device->programming_interface, 2) &&
	       append_text(node, " ") &&
	       append_hex(node, device->vendor_id, 4) &&
	       append_text(node, " ") && append_hex(node, device->device_id, 4);
}

static bool
info_pci_entry(const struct pci_device_info *device, void *context)
{
	struct kernel_info_report *node = context;
	return info_pci_identity(device, node) && append_text(node, "\n");
}

static bool
append_config_id(struct kernel_info_report *node,
    const struct pci_device_info *device, uint16_t offset, unsigned int bits)
{
	uint32_t value;
	if (!pci_config_read(device->segment, device->bus, device->device,
		device->function, offset, bits, &value))
		return append_text(node, "unavailable");
	return append_hex(node, value, bits / 4U);
}

static bool
info_device_properties(const struct pci_device_info *device,
    struct kernel_info_report *node, const char *description)
{
	if (!info_pci_identity(device, node) ||
	    !append_text(node, description) ||
	    !append_text(node, "  revision=") ||
	    !append_config_id(node, device, 0x08, 8) ||
	    !append_text(node, " command=") ||
	    !append_hex(node, device->command, 4))
		return false;
	/* Subsystem fields and these interrupt offsets apply to type-0 headers.
	 */
	if ((device->header_type & 0x7fU) == 0 &&
	    (!append_text(node, " subsystem=") ||
		!append_config_id(node, device, 0x2c, 16) ||
		!append_text(node, ":") ||
		!append_config_id(node, device, 0x2e, 16) ||
		!append_text(node, " irq-line=") ||
		!append_config_id(node, device, 0x3c, 8) ||
		!append_text(node, " irq-pin=") ||
		!append_config_id(node, device, 0x3d, 8)))
		return false;
	if (!append_text(node, "\n"))
		return false;
	for (unsigned int i = 0; i < 6; i++) {
		struct pci_bar_resource bar;
		if (!pci_bar_decode(device, i, &bar) ||
		    bar.type == PCI_BAR_UNUSED)
			continue;
		const char *type = bar.type == PCI_BAR_IO ? " IO 0x"
				   : bar.type == PCI_BAR_MEMORY64
				       ? " MMIO64 0x"
				       : " MMIO32 0x";
		if (!append_text(node, "  BAR") || !append_u64(node, i) ||
		    !append_text(node, type) ||
		    !append_hex(node, bar.base,
			bar.type == PCI_BAR_MEMORY64 ? 16 : 8) ||
		    !append_text(node, "\n"))
			return false;
		i += bar.slots - 1;
	}
	return true;
}

static bool
info_netpci_entry(const struct pci_device_info *device, void *context)
{
	if (device->class_code != 0x02)
		return true;
	return info_device_properties(device, context,
	    device->subclass == 0 ? " Ethernet\n" : " Network controller\n");
}

static bool
info_audiopci_entry(const struct pci_device_info *device, void *context)
{
	struct kernel_info_report *node = context;
	if (device->class_code != 0x04)
		return true;
	const char *type =
	    device->subclass == 0x03   ? " High Definition Audio\n"
	    : device->subclass == 0x01 ? " Audio controller (AC97/legacy)\n"
	    : device->subclass == 0x00 ? " Multimedia video function\n"
				       : " Multimedia function\n";
	if (!info_device_properties(device, node, type) ||
	    !append_text(node, "  status=") ||
	    !append_config_id(node, device, 0x06, 16) ||
	    !append_text(node, " header=") ||
	    !append_hex(node, device->header_type, 2) ||
	    !append_text(node, "\n  capabilities:"))
		return false;
	uint32_t status, next;
	if (pci_config_read(device->segment, device->bus, device->device,
		device->function, 0x06, 16, &status) &&
	    (status & 0x10) && (device->header_type & 0x7fU) == 0 &&
	    pci_config_read(device->segment, device->bus, device->device,
		device->function, 0x34, 8, &next)) {
		uint64_t seen = 0;
		for (unsigned int count = 0; next != 0 && count < 48; ++count) {
			if (next < 0x40 || next > 0xfc || (next & 3) ||
			    (seen & (UINT64_C(1) << (next / 4)))) {
				if (!append_text(node, " invalid-chain"))
					return false;
				break;
			}
			seen |= UINT64_C(1) << (next / 4);
			uint32_t cap;
			if (!pci_config_read(device->segment, device->bus,
				device->device, device->function,
				(uint16_t)next, 16, &cap))
				break;
			const char *name = (cap & 0xff) == 1	  ? "PM"
					   : (cap & 0xff) == 5	  ? "MSI"
					   : (cap & 0xff) == 0x11 ? "MSI-X"
					   : (cap & 0xff) == 0x10 ? "PCIe"
								  : "id";
			if (!append_text(node, " ") ||
			    !append_text(node, name) ||
			    !append_text(node, "=") ||
			    !append_hex(node, cap & 0xff, 2) ||
			    !append_text(node, "@") ||
			    !append_hex(node, next, 2))
				return false;
			next = (cap >> 8) & 0xff;
		}
	}
	if (!append_text(node, "\n  PCI config (offset: 32-bit words):\n"))
		return false;
	for (unsigned int offset = 0; offset < 256; offset += 32) {
		if (!append_text(node, "  ") || !append_hex(node, offset, 2) ||
		    !append_text(node, ":"))
			return false;
		for (unsigned int i = 0; i < 32; i += 4)
			if (!append_text(node, " ") ||
			    !append_config_id(node, device, offset + i, 32))
				return false;
		if (!append_text(node, "\n"))
			return false;
	}
	return append_text(node, "\n");
}

static void
info_cpuid(
    uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
	__asm__ volatile("cpuid"
	    : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
	    : "a"(leaf), "c"(0)
	    : "memory");
}

static void
info_store_word(char *buffer, size_t offset, uint32_t word)
{
	for (size_t i = 0; i < 4; i++)
		buffer[offset + i] = (char)(word >> (i * 8U));
}

static int
info_audiopci(struct kernel_info_report *node)
{
	if (!append_text(node, "Audio PCI diagnostic v1 (read-only)\n") ||
	    !pci_enumerate(info_audiopci_entry, node)) {
		return -EINVAL;
	}
	if (node->content_length ==
	    strlen("Audio PCI diagnostic v1 (read-only)\n") &&
	    !append_text(node, "No PCI multimedia functions found. "
			       "For USB audio also run usbconf.\n"))
		return -EINVAL;
	return 0;
}

static int
info_pci(struct kernel_info_report *node, bool network)
{
	if (!pci_enumerate(
		network ? info_netpci_entry : info_pci_entry, node)) {
		return -EINVAL;
	}
	return 0;
}

static int
info_usb(struct kernel_info_report *node)
{
	struct usb_host_device_info devices[USB_HOST_DEVICE_MAX];
	if (!append_text(node, "controllers: ") ||
	    !append_u64(node, usb_host_controller_count()) ||
	    !append_text(node, "\nactive: ") ||
	    !append_u64(node, usb_host_available()) || !append_text(node, "\n"))
		goto overflow;
	size_t count = usb_host_device_snapshot(devices, USB_HOST_DEVICE_MAX);
	for (size_t i = 0; i < count; i++)
		if (!append_text(node, devices[i].type) ||
		    !append_text(node, " ") ||
		    !append_u64(node, devices[i].index) ||
		    !append_text(node, "\n"))
			goto overflow;
	return 0;
overflow:
	return -EINVAL;
}

static int
info_disks(struct kernel_info_report *node)
{
	if (!append_text(node, "NAME SECTORS SECTOR_SIZE BYTES\n")) {
		return -EINVAL;
	}
	for (size_t i = 0; i < block_device_count(); i++) {
		struct block_device *device = block_device_at(i);
		if (device == NULL || !append_text(node, device->name) ||
		    !append_text(node, " ") ||
		    !append_u64(node, device->sector_count) ||
		    !append_text(node, " ") ||
		    !append_u64(node, device->logical_sector_size) ||
		    !append_text(node, " ") ||
		    !append_u64(node,
			device->sector_count * device->logical_sector_size) ||
		    !append_text(node, "\n")) {
			return -EINVAL;
		}
	}
	return 0;
}

static int
info_cpuinfo(struct kernel_info_report *node)
{
	uint32_t max_leaf, eax, ebx, ecx, edx;
	uint32_t signature = 0;
	uint32_t base_family, family, base_model, model, stepping;
	char vendor[13] = "UnknownCPU";
	char brand[49] = "Unknown processor";
	size_t brand_start = 0;
	info_cpuid(0, &max_leaf, &ebx, &ecx, &edx);
	if (max_leaf >= 1U) {
		info_store_word(vendor, 0, ebx);
		info_store_word(vendor, 4, edx);
		info_store_word(vendor, 8, ecx);
		vendor[12] = '\0';
		info_cpuid(1, &signature, &ebx, &ecx, &edx);
	}
	info_cpuid(0x80000000U, &max_leaf, &ebx, &ecx, &edx);
	if (max_leaf >= 0x80000004U) {
		for (uint32_t i = 0; i < 3U; i++) {
			uint32_t leaf = 0x80000002U + i;
			info_cpuid(leaf, &eax, &ebx, &ecx, &edx);
			info_store_word(brand, i * 16U, eax);
			info_store_word(brand, i * 16U + 4U, ebx);
			info_store_word(brand, i * 16U + 8U, ecx);
			info_store_word(brand, i * 16U + 12U, edx);
		}
		brand[48] = '\0';
	}
	for (size_t i = strlen(brand); i != 0 && brand[i - 1U] == ' '; i--)
		brand[i - 1U] = '\0';
	while (brand[brand_start] == ' ')
		brand_start++;
	base_family = (signature >> 8) & 0x0fU;
	base_model = (signature >> 4) & 0x0fU;
	family = base_family == 0x0fU
		     ? base_family + ((signature >> 20) & 0xffU)
		     : base_family;
	model = (base_family == 0x06U || base_family == 0x0fU)
		    ? base_model | (((signature >> 16) & 0x0fU) << 4)
		    : base_model;
	stepping = signature & 0x0fU;
	if (!append_text(node, "processors discovered: ") ||
	    !append_u64(node, smp_discovered_count()) ||
	    !append_text(node, "\nprocessors online: ") ||
	    !append_u64(node, smp_online_count()) ||
	    !append_text(node, "\nprocessor: 0\nvendor_id: ") ||
	    !append_text(node, vendor) ||
	    !append_text(node, "\ncpu family: ") || !append_u64(node, family) ||
	    !append_text(node, "\nmodel: ") || !append_u64(node, model) ||
	    !append_text(node, "\nstepping: ") || !append_u64(node, stepping) ||
	    !append_text(node, "\nmodel name: ") ||
	    !append_text(node, brand + brand_start) ||
	    !append_text(node, "\n")) {
		return -EINVAL;
	}
	return 0;
}

static int
info_modules(struct kernel_info_report *node)
{
	struct kernel_module_snapshot_entry entries[KERNEL_MODULE_LIMIT];
	size_t count;
	count = kernel_module_snapshot(
	    entries, sizeof(entries) / sizeof(entries[0]));
	for (size_t i = 0; i < count; i++) {
		if (!append_text(node, entries[i].name) ||
		    !append_text(node, "\n")) {
			return -EINVAL;
		}
	}
	return 0;
}

int
kernel_info_snapshot(
    enum kernel_info_kind kind, struct kernel_info_report *report)
{
	int result;
	if (report == NULL)
		return -EINVAL;
	report->content_length = 0;
	switch (kind) {
	case KERNEL_INFO_CPU:
		result = info_cpuinfo(report);
		break;
	case KERNEL_INFO_MODULES:
		result = info_modules(report);
		break;
	case KERNEL_INFO_PCI:
		result = info_pci(report, false);
		break;
	case KERNEL_INFO_NETPCI:
		result = info_pci(report, true);
		break;
	case KERNEL_INFO_AUDIOPCI:
		result = info_audiopci(report);
		break;
	case KERNEL_INFO_USB:
		result = info_usb(report);
		break;
	case KERNEL_INFO_DISKS:
		result = info_disks(report);
		break;
	default:
		return -EINVAL;
	}
	if (result == 0)
		report->content[report->content_length] = 0;
	return result;
}
