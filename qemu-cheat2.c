/*
 * KVM API Sample.
 * author: Xu He Jie xuhj@cn.ibm.com
 */
#include <stdio.h>
#include <memory.h>
#include <sys/mman.h>
#include <pthread.h>
#include <linux/kvm.h>
#include <stdlib.h>
#include <assert.h>

#include <fcntl.h> // for open
#include <unistd.h> // for close
#include <sys/ioctl.h>

#define KVM_DEVICE "/dev/kvm"
#define RAM_SIZE 512000000
#define CODE_START 0x1000
#define BINARY_FILE "img2.bin"

struct kvm {
   int dev_fd;		// /dev/kvm 的句柄
   int vm_fd;		// GUEST 的句柄
   __u64 ram_size;	// GUEST 的内存大小
   __u64 ram_start;	// GUEST 的内存起始地址，
			// 这个地址是qemu emulator通过mmap映射的地址
   int kvm_version;	
   struct kvm_userspace_memory_region mem;	// slot 内存结构，由用户空间填充
						// 允许对guest的地址做分段。将多个slot组成线性地址

   struct vcpu *vcpus;	// vcpu 数组
   int vcpu_number;	// vcpu 个数
};

struct vcpu {
    int vcpu_id;                 // vCPU id，vCPU
    int vcpu_fd;                 // vCPU 句柄
    pthread_t vcpu_thread;       // vCPU 线程句柄
    struct kvm_run *kvm_run;     // KVM 运行时结构，也可以看做是上下文
    int kvm_run_mmap_size;       // 运行时结构大小
    struct kvm_regs regs;        // vCPU的寄存器
    struct kvm_sregs sregs;      // vCPU的特殊寄存器
    void *(*vcpu_thread_func)(void *);  // 线程执行函数
};

void kvm_reset_vcpu (struct vcpu *vcpu);

void *kvm_cpu_thread(void *data) {
    // 获取参数
    struct kvm *kvm = (struct kvm *)data;
    int ret = 0;
    // 设置KVM的参数
    kvm_reset_vcpu(kvm->vcpus);

    while (1) {
        printf("KVM start run\n");
        // 启动虚拟机，此时的虚拟机已经有内存和CPU了，可以运行起来了。
        ret = ioctl(kvm->vcpus->vcpu_fd, KVM_RUN, 0);

        if (ret < 0) {
            fprintf(stderr, "KVM_RUN failed\n");
            exit(1);
        }

        // 前文 kvm_init_vcpu 函数中，将 kvm_run 关联了 vCPU 结构的内存
        // 所以这里虚拟机退出的时候，可以获取到 exit_reason，虚拟机退出原因
        switch (kvm->vcpus->kvm_run->exit_reason) {
        case KVM_EXIT_UNKNOWN:
            printf("KVM_EXIT_UNKNOWN\n");
            break;
        case KVM_EXIT_DEBUG:
            printf("KVM_EXIT_DEBUG\n");
            break;
        // 虚拟机执行了IO操作，虚拟机模式下的CPU会暂停虚拟机并
        // 把执行权交给emulator
        case KVM_EXIT_IO:
            printf("KVM_EXIT_IO\n");
            printf("out port: %d, data: %d\n", 
                kvm->vcpus->kvm_run->io.port,  
                *(int *)((char *)(kvm->vcpus->kvm_run) + kvm->vcpus->kvm_run->io.data_offset)
                );
            sleep(1);
            break;
        // 虚拟机执行了memory map IO操作
        case KVM_EXIT_MMIO:
            printf("KVM_EXIT_MMIO\n");
            break;
        case KVM_EXIT_INTR:
            printf("KVM_EXIT_INTR\n");
            break;
        case KVM_EXIT_SHUTDOWN:
            printf("KVM_EXIT_SHUTDOWN\n");
            goto exit_kvm;
            break;
        default:
            printf("KVM PANIC\n");
            goto exit_kvm;
        }
    }

exit_kvm:
    return 0;
}

void kvm_reset_vcpu (struct vcpu *vcpu) {
    if (ioctl(vcpu->vcpu_fd, KVM_GET_SREGS, &(vcpu->sregs)) < 0) {
        perror("can not get sregs\n");
        exit(1);
    }
    // #define CODE_START 0x1000
    /* sregs 结构体
        x86
        struct kvm_sregs {
            struct kvm_segment cs, ds, es, fs, gs, ss;
            struct kvm_segment tr, ldt;
            struct kvm_dtable gdt, idt;
            __u64 cr0, cr2, cr3, cr4, cr8;
            __u64 efer;
            __u64 apic_base;
            __u64 interrupt_bitmap[(KVM_NR_INTERRUPTS + 63) / 64];
        };
    */
    // cs 为code start寄存器，存放了程序的起始地址
    vcpu->sregs.cs.selector = CODE_START;
    vcpu->sregs.cs.base = CODE_START * 16;
    // ss 为堆栈寄存器，存放了堆栈的起始位置
    vcpu->sregs.ss.selector = CODE_START;
    vcpu->sregs.ss.base = CODE_START * 16;
    // ds 为数据段寄存器，存放了数据开始地址
    vcpu->sregs.ds.selector = CODE_START;
    vcpu->sregs.ds.base = CODE_START *16;
    // es 为附加段寄存器
    vcpu->sregs.es.selector = CODE_START;
    vcpu->sregs.es.base = CODE_START * 16;
    // fs, gs 同样为段寄存器
    vcpu->sregs.fs.selector = CODE_START;
    vcpu->sregs.fs.base = CODE_START * 16;
    vcpu->sregs.gs.selector = CODE_START;

    // 为vCPU设置以上寄存器的值
    if (ioctl(vcpu->vcpu_fd, KVM_SET_SREGS, &vcpu->sregs) < 0) {
        perror("can not set sregs");
        exit(1);
    }

    // 设置寄存器标志位
    vcpu->regs.rflags = 0x0000000000000002ULL;
    // rip 表示了程序的起始指针，地址为 0x0000000
    // 在加载镜像的时候，我们直接将binary读取到了虚拟机的内存起始位
    // 所以虚拟机开始的时候会直接运行binary
    vcpu->regs.rip = 0;
    // rsp 为堆栈顶
    vcpu->regs.rsp = 0xffffffff;
    // rbp 为堆栈底部
    vcpu->regs.rbp= 0;

    if (ioctl(vcpu->vcpu_fd, KVM_SET_REGS, &(vcpu->regs)) < 0) {
        perror("KVM SET REGS\n");
        exit(1);
    }
}

void load_binary(struct kvm *kvm) {
    int fd = open(BINARY_FILE, O_RDONLY); 	// 打开这个二进制文件(镜像）

    if (fd < 0) {
        fprintf(stderr, "can not open binary file\n");
        exit(1);
    }

    int ret = 0;
    char *p = (char *)kvm->ram_start;

    while(1) {
        ret = read(fd, p, 4096);		// 将镜像内容加载到虚拟机的内存中
        if (ret <= 0) {
            break;
        }
        printf("read size: %d", ret);
        p += ret;
    }
}

struct kvm *kvm_init(void) {
    struct kvm *kvm = malloc(sizeof(struct kvm));
    kvm->dev_fd = open(KVM_DEVICE, O_RDWR);	// 打开 /dev/kvm 获取 kvm 句柄

    if (kvm->dev_fd < 0) {
        perror("open kvm device fault: ");
        return NULL;
    }

    kvm->kvm_version = ioctl(kvm->dev_fd, KVM_GET_API_VERSION, 0);	// 获取 kvm API 版本

    return kvm;
}

void kvm_clean(struct kvm *kvm) {
    assert (kvm != NULL);
    close(kvm->dev_fd);
    free(kvm);
}

int kvm_create_vm(struct kvm *kvm, int ram_size) {
    int ret = 0;
    kvm->vm_fd = ioctl(kvm->dev_fd, KVM_CREATE_VM, 0);

    if (kvm->vm_fd < 0) {
        perror("can not create vm");
        return -1;
    }

    // 为 kvm 分配内存。通过系统调用.
    kvm->ram_size = ram_size;
    kvm->ram_start =  (__u64)mmap(NULL, kvm->ram_size, 
                PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, 
                -1, 0);

    if ((void *)kvm->ram_start == MAP_FAILED) {
        perror("can not mmap ram");
        return -1;
    }
    
    // kvm->mem 结构需要初始化后传递给 KVM_SET_USER_MEMORY_REGION 接口
    // 只有一个内存槽
    kvm->mem.slot = 0;
    // guest 物理内存起始地址
    kvm->mem.guest_phys_addr = 0;
    // 虚拟机内存大小
    kvm->mem.memory_size = kvm->ram_size;
    // 虚拟机内存在host上的用户空间地址，这里就是绑定内存给guest
    kvm->mem.userspace_addr = kvm->ram_start;

    // 调用 KVM_SET_USER_MEMORY_REGION 为虚拟机分配内存。
    ret = ioctl(kvm->vm_fd, KVM_SET_USER_MEMORY_REGION, &(kvm->mem));

    if (ret < 0) {
        perror("can not set user memory region");
        return ret;
    }

    return ret;
}

void kvm_clean_vm(struct kvm *kvm) {
    close(kvm->vm_fd);
    munmap((void *)kvm->ram_start, kvm->ram_size);
}

struct vcpu *kvm_init_vcpu(struct kvm *kvm, int vcpu_id, void *(*fn)(void *)) {
    // 申请vcpu结构
    struct vcpu *vcpu = malloc(sizeof(struct vcpu));
    // 只有一个 vCPU，所以这里只初始化一个
    vcpu->vcpu_id = 0;
    // 调用 KVM_CREATE_VCPU 获取 vCPU 句柄，并关联到kvm->vm_fd（由KVM_CREATE_VM返回）
    vcpu->vcpu_fd = ioctl(kvm->vm_fd, KVM_CREATE_VCPU, vcpu->vcpu_id);

    if (vcpu->vcpu_fd < 0) {
        perror("can not create vcpu");
        return NULL;
    }

    // 获取KVM运行时结构大小
    vcpu->kvm_run_mmap_size = ioctl(kvm->dev_fd, KVM_GET_VCPU_MMAP_SIZE, 0);

    if (vcpu->kvm_run_mmap_size < 0) {
        perror("can not get vcpu mmsize");
        return NULL;
    }

    printf("%d\n", vcpu->kvm_run_mmap_size);
    // 将 vcpu_fd 的内存映射给 vcpu->kvm_run结构。相当于一个关联操作
    // 以便能够在虚拟机退出的时候获取到vCPU的返回值等信息
    vcpu->kvm_run = mmap(NULL, vcpu->kvm_run_mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, vcpu->vcpu_fd, 0);

    if (vcpu->kvm_run == MAP_FAILED) {
        perror("can not mmap kvm_run");
        return NULL;
    }

    // 设置线程执行函数
    vcpu->vcpu_thread_func = fn;
    return vcpu;
}

void kvm_clean_vcpu(struct vcpu *vcpu) {
    munmap(vcpu->kvm_run, vcpu->kvm_run_mmap_size);
    close(vcpu->vcpu_fd);
}

void kvm_run_vm(struct kvm *kvm) {
    int i = 0;

    for (i = 0; i < kvm->vcpu_number; i++) {
         // 启动线程执行 vcpu_thread_func 并将 kvm 结构作为参数传递给线程
        if (pthread_create(&(kvm->vcpus->vcpu_thread), (const pthread_attr_t *)NULL, kvm->vcpus[i].vcpu_thread_func, kvm) != 0) {
            perror("can not create kvm thread");
            exit(1);
        }
    }

    pthread_join(kvm->vcpus->vcpu_thread, NULL);
}

int main(int argc, char **argv) {
    int ret = 0;
    // 初始化kvm结构体
    struct kvm *kvm = kvm_init();

    if (kvm == NULL) {
        fprintf(stderr, "kvm init fauilt\n");
        return -1;
    }

    // 创建VM，并分配内存空间
    if (kvm_create_vm(kvm, RAM_SIZE) < 0) {
        fprintf(stderr, "create vm fault\n");
        return -1;
    }

    load_binary(kvm);

    // only support one vcpu now
    kvm->vcpu_number = 1;
    // 创建执行现场
    kvm->vcpus = kvm_init_vcpu(kvm, 0, kvm_cpu_thread);

    // 启动虚拟机
    kvm_run_vm(kvm);

    kvm_clean_vm(kvm);
    kvm_clean_vcpu(kvm->vcpus);
    kvm_clean(kvm);
}
