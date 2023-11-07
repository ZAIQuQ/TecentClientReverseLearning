// Ptrace-based injector
#include <stdio.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/types.h>
#include <link.h>
#include <dlfcn.h>

typedef unsigned long ul;
const char libdl_dir[] = "/apex/com.android.runtime/lib/bionic/libdl.so"; // dlopen������libdl.so�ļ���
const char libc_dir[] = "/apex/com.android.runtime/lib/bionic/libc.so"; // malloc������libc.so�ļ���

void *d1open(pid_t pid, char *filename, int flag);

void *mall0c(pid_t pid, size_t len);

int ptrace_poketext(pid_t pid, size_t len, void *text, void *addr);

void *get_lib_base(pid_t pid, const char *lib_dir);

int main(int argc, const char *argv[])
{
    /* 
        ��һ�������ǽ�����(apk����)
        �ڶ�����������Ҫע���.so���ļ�·��
    */
    pid_t pid = 0;
    FILE *fp = NULL;
    char process[256] = "";
    char pbuffer[256] = "";
    if (argc != 3)
    {
        printf("usage:\n%s [process_name] [lib_path]\n", argv[0]);
        return 0;
    }
    /// 1. ��ȡ����pid
    sprintf(process, "pidof %s", argv[1]); // ��shellָ��õ�pid
    if((fp = popen(process, "r")) == NULL )
    {
        printf("[WARNING] process not found !\n");
        return -1;
    }
    fread(pbuffer, 1, 256, fp);
    sscanf(pbuffer, "%d", &pid);
    printf("[MESSAGE] pid is %d\n", pid);
    pclose(fp);

    /// 2. ���ӵ�����
    const char *lib_dir = argv[2];
    if(ptrace(PTRACE_ATTACH, pid, NULL, NULL) < 0)
    {
        perror("[WARNING] failed to attach to the process !\n");
        return -1;
    }

    int status;

    waitpid(pid, &status, 0);

    printf("[MESSAGE] STATUS : 0x%x\n", status); // 0x137f

    printf("[MESSAGE] succeed to attach to the process !\n");

    /// 3. Զ�̺�������
   
    // Զ�̵���malloc��Ϊhook�����ڴ棬д��lib�ļ�·��

    void *buffer = mall0c(pid, 0x800);
    if( buffer == NULL )
    {
        perror("[WARNING] malloc failed !\n");

        printf("\n getchar() debugging ......\n");
        getchar();

        ptrace(PTRACE_CONT, pid, NULL, NULL);
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return -1;
    }

    // ��hook libд������ڴ�

    ptrace_poketext(pid, strlen(lib_dir) + 1, (void *)lib_dir, buffer);

    // while(getchar() == 'q')
    //     ;

    // ��lib�ļ����ص�����

    if( d1open(pid, buffer, RTLD_LAZY) == NULL )
    {
        perror("[WARNING] load lib to process failed !\n");
        return -1;
    }

    /// 4. �������
    printf("[MESSAGE] injection success !\n");
    ptrace(PTRACE_CONT, pid, NULL, NULL);
    ptrace(PTRACE_DETACH, pid, NULL, NULL);

    // printf("\n getchar() debugging ......\n");
    // getchar();

    return 0;
}

void *d1open(pid_t pid, char *filename, int flag)
{
    /*
        dlopen 2������ �ֱ�ѹ��R0��R1��
        ����ֵ��R0��
        LR�Ĵ����淵�ص�ַ
        �ֶ����κ��޸�PC�Ĵ���ΪĿ�꺯����ַ���޸�LR�Ĵ���Ϊ0
        ��������ʱ�����쳣����ȡ����ֵ
    */

    int status;
    struct user_regs pushed_regs;
    struct user_regs regs;
    void *local_lib_base;
    void *local_dlopen_addr;
    void *remote_lib_base;
    ul dlopen_offset;
    void *dlopen_addr;
    
    ptrace(PTRACE_GETREGS, pid, NULL, (void *)&pushed_regs);

    memcpy(&regs, &pushed_regs, sizeof(struct user_regs)); // �Ĵ���ѹջ
    dlopen_offset = 0x1849; // ��IDA��ȡelf�ļ�libdl.so�۲�ó�


    // ����dlopen��Զ�̽����еĵ�ַ
    remote_lib_base = get_lib_base(pid, libdl_dir);
    dlopen_addr = remote_lib_base + dlopen_offset;

    regs.uregs[13] -= 0x50;
    regs.uregs[15] = ((ul)dlopen_addr & 0xFFFFFFFE); // ������ַ���λ����������ģʽ
    regs.uregs[0] = (ul)filename; // R0
    regs.uregs[1] = (ul)flag; // R1

    // ����û����
    // while(getchar() == 'q')
    //     ;

    printf("[MESSAGE] Remote call: dlopen(0x%lx, 0x%lx)\n",(ul)filename, (ul)flag);

    // ֮ǰ��filename�����⣬���ڽ����
    // while(getchar() == 'q')
    //     ;

    if ((ul)dlopen_addr & 0x1) // PSR�Ĵ�������5λΪ1��Thumbģʽ��Ϊ0��ARMģʽ
    {
        regs.uregs[16] = regs.uregs[16] | 0x20;
    }
    else
    {
        regs.uregs[16] = regs.uregs[16] & 0xFFFFFFDF;
    }
    regs.uregs[14] = 0; // LR�Ĵ���

    if(ptrace(PTRACE_SETREGS, pid, NULL, (void *)&regs) != 0 || ptrace(PTRACE_CONT, pid, NULL, NULL) != 0 )
    {
        perror("[WARNING] call dlopen() failed !\n");
        return NULL;
    }

    waitpid(pid, &status, WUNTRACED);

    printf("[MESSGAE] STATUS: 0x%x\n", status);

    while (status != 0xb7f) //�����򷵻ص�ַΪ0�����쳣�����صĴ�����: 0xb7f
    {
        ptrace(PTRACE_CONT, pid, NULL, NULL);
        waitpid(pid, &status, WUNTRACED);
    }

    ptrace(PTRACE_GETREGS, pid, NULL, (void *)&regs);

    void *ret = (void *)regs.uregs[0];

    // �Ĵ�����ջ
    ptrace(PTRACE_SETREGS, pid, NULL, (void *)&pushed_regs);

    printf("[MESSAGE] dlopen ret handle: 0x%lx\n", (ul)ret);
    return ret;
}


void *mall0c(pid_t pid, size_t len)
{
    /* 
        malloc 1��������ѹ��R0��
        ����ֵ��R0��
        LR�Ĵ����淵�ص�ַ
        �ֶ����κ��޸�PC�Ĵ���ΪĿ�꺯����ַ���޸�LR�Ĵ���Ϊ0
        ��������ʱ�����쳣����ȡ����ֵ
    */
    int status;
    struct user_regs pushed_regs;
    struct user_regs regs;
    ul malloc_offset;
    void *remote_lib_base;
    void *malloc_addr;

    malloc_offset = 0x2d685; // ��IDA��ȡelf�ļ�libc.so�۲�ó�


    // ����malloc��Զ�̽����еĵ�ַ
    remote_lib_base = get_lib_base(pid, libc_dir);
    malloc_addr = remote_lib_base + malloc_offset;

    dlerror();
    if( ptrace(PTRACE_GETREGS, pid, NULL, (void *)&pushed_regs) < 0 )
    {
        perror("[WARNING] failed to push remote regs \n");
    } 
    memcpy(&regs, &pushed_regs, sizeof(struct user_regs)); // �Ĵ���ѹջ
    regs.uregs[13] -= 0x50; // ��ջѹ80λ
    regs.uregs[15] = ((ul)malloc_addr & 0xFFFFFFFE); // PC�Ĵ�������ַ���λ����������ģʽ
    regs.uregs[0] = (ul)len; // R0
    printf("[MESSAGE] Remote call: malloc(0x%lx)\n", (ul)len);
    if ((ul)malloc_addr & 0x1) // PSR�Ĵ�������5λΪ1��Thumbģʽ��Ϊ0��ARMģʽ
    {
        regs.uregs[16] = regs.uregs[16] | 0x20;
    }
    else
    {
        regs.uregs[16] = regs.uregs[16] & 0xFFFFFFDF;
    }
    regs.uregs[14] = 0; // LR�Ĵ���

    if(ptrace(PTRACE_SETREGS, pid, NULL, (void *)&regs) < 0 || ptrace(PTRACE_CONT, pid, NULL, NULL) < 0 )
    {
        perror("[WARNING] call malloc() failed !\n");
        return NULL;
    }

    waitpid(pid, &status, WUNTRACED);
    while (status != 0xb7f) // �����򷵻ص�ַΪ0�����쳣�����صĴ�����: 0xb7f
    {
        ptrace(PTRACE_CONT, pid, NULL, NULL);
        waitpid(pid, &status, WUNTRACED);
    }

    ptrace(PTRACE_GETREGS, pid, NULL, (void *)&regs);

    void *ret = (void *)regs.uregs[0];

    // �Ĵ�����ջ
    ptrace(PTRACE_SETREGS, pid, NULL, (void *)&pushed_regs);

    printf("[MESSAGE] malloc ret mem: 0x%lx\n", (ul)ret);
    return ret;
}

int ptrace_poketext(pid_t pid, size_t len, void *text, void *addr)
{
    /*
        ��Զ�̽��̵�ָ���ڴ�д������
    */
    const int batch_size = 4; // һ��4���ֽ�
    unsigned int tmp_text;
    size_t writen;
    for (writen = 0; writen < len; writen += batch_size)
    {
        if (len - writen >= 4)
        {
            tmp_text = *(unsigned int *)(text + writen);
        }
        else
        {
            // ����4���ֽ�ʱ����Ҫ�ȶ�ȡԭ���ݣ�����ԭ��ַ���ĸ�λ����
            tmp_text = ptrace(PTRACE_PEEKDATA, pid, (void *)(addr + writen), NULL);
            for (int i = 0; i < len - writen; i++)
            {
                *(((unsigned char *)(&tmp_text)) + i) = *(unsigned char *)(text + writen + i);
            }
        }
        if (ptrace(PTRACE_POKEDATA, pid, (void *)(addr + writen), (void *)(tmp_text)) < 0)
        {
            return -1;
        }
    }
    return len;
}

void *get_lib_base(pid_t pid, const char *lib_dir)
{
    /*
        ��ȡĿ��lib�ļ��Ļ���ַ
        ��pid = 0, ��ȡlocal���̵Ļ���ַ
        ��pid > 0, ��ȡԶ�̽��̵Ļ���ַ
    */
    FILE *fp = NULL;
    char maps_path[1024] = "";
    char maps_line[1024] = "";
    void *lib_base = NULL;

    if(pid) sprintf(maps_path, "/proc/%d/maps", pid);
    else sprintf(maps_path, "/proc/self/maps");
    fp = fopen(maps_path, "rt");
    while(fgets(maps_line, 1024, fp) != NULL)
    {
        if(strstr(maps_line, lib_dir) != NULL)
        {
            sscanf(maps_line, "%lx", (unsigned long *)(&lib_base));
            break;
        }
    }
    fclose(fp);
    if(lib_base == NULL)
    {
        printf("[WARNING] %s not found in process %d\n", lib_dir, pid);
        return NULL;
    }

    printf("[MESSAGE] %s at mem 0x%lx in process %d\n", lib_dir, (unsigned long)lib_base, pid);

    return lib_base;
}