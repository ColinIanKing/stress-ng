/*
 * Copyright (C) 2021-2025 Colin Ian King
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */
#include "stress-ng.h"
#include "core-arch.h"
#include "core-builtin.h"
#include "core-capabilities.h"
#include "core-madvise.h"
#include "core-mmap.h"

#include <sys/ioctl.h>

#if defined(HAVE_LINUX_KVM_H)
#include <linux/kvm.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"kvm N",	"start N workers exercising /dev/kvm" },
	{ NULL, "kvm-ops N",	"stop after N kvm create/run/destroy operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(__linux__)	&&			\
    defined(HAVE_LINUX_KVM_H) && 		\
    defined(KVM_CREATE_VM) &&			\
    defined(KVM_SET_USER_MEMORY_REGION) &&	\
    defined(KVM_CREATE_VCPU) && 		\
    defined(KVM_GET_SREGS) &&			\
    defined(KVM_SET_SREGS) &&			\
    defined(KVM_SET_REGS) &&			\
    defined(KVM_GET_VCPU_MMAP_SIZE) &&		\
    defined(KVM_RUN) &&				\
    defined(KVM_EXIT_IO) &&			\
    defined(KVM_EXIT_SHUTDOWN) &&		\
    defined(STRESS_ARCH_X86) &&			\
    !defined(__i386__) &&			\
    !defined(__i386)

static int stress_kvm_open(const char *name, const bool report)
{
	int kvm_fd;

	if ((kvm_fd = open("/dev/kvm", O_RDWR)) < 0) {
		switch (errno) {
		case ENOENT:
			if (report)
				pr_inf_skip("%s: /dev/kvm not available, skipping stressor\n",
					name);
			break;
		case EPERM:
		case EACCES:
			if (report && !stress_check_capability(SHIM_CAP_SYS_ADMIN))
				pr_inf_skip("%s stressor will be skipped, "
					"need to be running with CAP_SYS_ADMIN "
					"rights for this stressor\n", name);
			break;
		default:
			if (report)
				pr_fail("%s: open /dev/kvm failed, errno=%d (%s), skipping stressor\n",
					name, errno, strerror(errno));
			break;
		}
		return -1;
	}
	return kvm_fd;
}

static int stress_kvm_supported(const char *name)
{
	int kvm_fd;

	kvm_fd = stress_kvm_open(name, true);
	if (kvm_fd < 0)
		return -1;

	(void)close(kvm_fd);
	return 0;
}

/*
 *  Minimal x86 kernel, read/increment/write port $80 loop
 */
static const uint8_t kvm_x86_kernel[] = {
	0x31, 0xc0,  /* xor    %eax,%eax */
	0x0f, 0xa2,  /* cpuid */
	0x0f, 0x31,  /* rdtsc */
	0x0f, 0x08,  /* inv */
	0xe5, 0x80,  /* in     $0x80,%eax */
	0x40,        /* inc    %eax       */
	0xe7, 0x80,  /* out    %eax,$0x80 */
	0xeb, 0xf1,  /* jmp    0 <_start> */
};

/*
 *  stress_kvm
 *	stress /dev/kvm
 */
static int stress_kvm(stress_args_t *args)
{
	bool pr_version = false;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		int kvm_fd, vm_fd, vcpu_fd, version, ret, i;
		void *vm_mem;
		const size_t vm_mem_size = (stress_mwc16() + 2) * args->page_size;
		ssize_t run_size;
		struct kvm_userspace_memory_region kvm_mem;
		struct kvm_sregs sregs;
		struct kvm_regs regs;
		struct kvm_run *run;
		bool run_ok = false;
		uint8_t value = 0;

		kvm_fd = stress_kvm_open(args->name, stress_instance_zero(args));
		if (kvm_fd < 0)
			return EXIT_NOT_IMPLEMENTED;

#if defined(KVM_GET_API_VERSION)
		version = ioctl(kvm_fd, KVM_GET_API_VERSION, 0);
		if ((!pr_version) && (stress_instance_zero(args))) {
			pr_dbg("%s: KVM kernel API version %d\n", args->name, version);
			pr_version = true;
		}
#endif

		vm_fd = ioctl(kvm_fd, KVM_CREATE_VM, 0);
		if (vm_fd < 0) {
			if (errno == EINTR)
				goto tidy_kvm_fd;
			if (errno == EBUSY) {
				pr_inf_skip("%s: KVM device busy, skipping stressor\n", args->name);
				(void)close(kvm_fd);
				return EXIT_NO_RESOURCE;
			}
			pr_fail("%s: ioctl KVM_CREATE_VM failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto tidy_kvm_fd;
		}
		vm_mem = stress_mmap_populate(NULL, vm_mem_size,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
			-1, 0);
		if (vm_mem == MAP_FAILED)
			goto tidy_vm_fd;
		stress_set_vma_anon_name(vm_mem, vm_mem_size, "vm-memory");
		(void)stress_madvise_mergeable(vm_mem, vm_mem_size);

		(void)shim_memset(&kvm_mem, 0, sizeof(kvm_mem));
		kvm_mem.slot = 0;
		kvm_mem.guest_phys_addr = 0;
		kvm_mem.memory_size = vm_mem_size;
		kvm_mem.userspace_addr = (uintptr_t)vm_mem;

		ret = ioctl(vm_fd, KVM_SET_USER_MEMORY_REGION, &kvm_mem);
		if (ret < 0) {
			if (errno == EINTR)
				goto tidy_vm_mmap;
			pr_fail("%s: ioctl KVM_SET_USER_MEMORY_REGION failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto tidy_vm_mmap;
		}

		vcpu_fd = ioctl(vm_fd, KVM_CREATE_VCPU, 0);
		if (vcpu_fd < 0) {
			if (errno == EINTR)
				goto tidy_vm_mmap;
			pr_fail("%s: ioctl KVM_CREATE_VCPU failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto tidy_vm_mmap;
		}

		(void)shim_memcpy(vm_mem, &kvm_x86_kernel, sizeof(kvm_x86_kernel));
		if (ioctl(vcpu_fd, KVM_GET_SREGS, &sregs) < 0) {
			if (errno == EINTR)
				goto tidy_vcpu_fd;
			pr_fail("%s: ioctl KVM_GET_SREGS failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto tidy_vcpu_fd;
		}

		sregs.cs.selector = 0;
		sregs.cs.base = 0;
		sregs.ds.selector = 0;
		sregs.ds.base = 0;
		sregs.es.selector = 0;
		sregs.es.base = 0;
		sregs.fs.selector = 0;
		sregs.fs.base = 0;
		sregs.gs.selector = 0;
		sregs.gs.base = 0;
		sregs.ss.selector = 0;
		sregs.ss.base = 0;

		if (ioctl(vcpu_fd, KVM_SET_SREGS, &sregs) < 0) {
			pr_fail("%s: ioctl KVM_SET_SREGS failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto tidy_vcpu_fd;
		}

		(void)shim_memset(&regs, 0, sizeof(regs));
		regs.rflags = 2;
		regs.rip = 0;
		if (ioctl(vcpu_fd, KVM_SET_REGS, &regs) < 0) {
			if (errno == EINTR)
				goto tidy_vcpu_fd;
			pr_fail("%s: ioctl KVM_SET_REGS failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto tidy_vcpu_fd;
		}

		run_size = (ssize_t)ioctl(kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
		if (run_size < 0) {
			if (errno == EINTR)
				goto tidy_vcpu_fd;
			pr_fail("%s: ioctl KVM_GET_VCPU_MMAP_SIZE failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto tidy_vcpu_fd;
		}

		run = (struct kvm_run *)stress_mmap_populate(NULL, (size_t)run_size,
			PROT_READ | PROT_WRITE, MAP_SHARED, vcpu_fd, 0);
		if (run == MAP_FAILED) {
			pr_fail("%s: mmap on vcpu_fd failed%s, errno=%d (%s)\n",
				args->name, stress_get_memfree_str(),
				errno, strerror(errno));
			goto tidy_vcpu_fd;
		}
		stress_set_vma_anon_name(run, (size_t)run_size, "kvm-run");

		for (i = 0; LIKELY((i < 1000) && stress_continue(args)); i++) {
			uint8_t *port;

			ret = ioctl(vcpu_fd, KVM_RUN, 0);
			if (ret < 0) {
				if (errno != EINTR) {
					pr_fail("%s: ioctl KVM_RUN failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				}
				goto tidy_run;
			}
			switch (run->exit_reason) {
			case KVM_EXIT_IO:
				port = (uint8_t *)run + run->io.data_offset;
				if (run->io.direction == 0) {
					/* Read */
					*port = value;
				} else {
					/* Write */
					value = *port;
				}
				if (*port == 0xff) {
					run_ok = true;
					goto tidy_run;
				}
				break;
			case KVM_EXIT_SHUTDOWN:
				goto tidy_run;
			default:
				break;
			}
#if defined(KVM_GET_REGS)
			{
				struct kvm_regs kregs;

				VOID_RET(int, ioctl(vcpu_fd, KVM_GET_REGS, &kregs));
			}
#endif
#if defined(KVM_GET_FPU)
			{
				struct kvm_fpu fpu;

				VOID_RET(int, ioctl(vcpu_fd, KVM_GET_FPU, &fpu));
			}
#endif
#if defined(KVM_GET_MP_STATE)
			{
				struct kvm_mp_state state;

				VOID_RET(int, ioctl(vcpu_fd, KVM_GET_MP_STATE, &state));
			}
#endif
#if defined(KVM_GET_XSAVE)
			{
				struct kvm_xsave xsave;

				VOID_RET(int, ioctl(vcpu_fd, KVM_GET_XSAVE, &xsave));
			}
#endif
#if defined(KVM_GET_TSC_KHZ)
			VOID_RET(int, ioctl(vcpu_fd, KVM_GET_TSC_KHZ, 0));
#endif
		}
tidy_run:
		(void)munmap((void *)run, (size_t)run_size);
tidy_vcpu_fd:
		(void)close(vcpu_fd);
tidy_vm_mmap:
		(void)munmap((void *)vm_mem, vm_mem_size);
tidy_vm_fd:
		(void)close(vm_fd);
tidy_kvm_fd:
		(void)close(kvm_fd);
		if (run_ok)
			stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_kvm_info = {
	.stressor = stress_kvm,
	.classifier = CLASS_DEV | CLASS_OS,
	.supported = stress_kvm_supported,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_kvm_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_DEV | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built on non-x86-64 without linux/kvm.h"
};
#endif
