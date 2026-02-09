#define SBRK_ERROR ((char *)-1)

struct stat;

// system calls
int fork(void);
int exit(int) __attribute__((noreturn));
int wait(int*);
int pipe(int*);
int write(int, const void*, int);
int read(int, void*, int);
int close(int);
int kill(int);
int exec(const char*, char**);
int open(const char*, int);
int mknod(const char*, short, short);
int unlink(const char*);
int fstat(int fd, struct stat*);
int link(const char*, const char*);
int mkdir(const char*);
int chdir(const char*);
int dup(int);
int getpid(void);
char* sys_sbrk(int,int);
int pause(int);
int uptime(void);

// slab allocator syscalls
typedef unsigned long uint64;
typedef uint64 size_t;
typedef void* kmem_cache_t;
#define BLOCK_SIZE 4096

int kmem_init(int);
kmem_cache_t kmem_cache_create(const char*, int, int, int);
uint64 kmem_cache_alloc(kmem_cache_t);
int kmem_cache_free(kmem_cache_t, uint64);
int kmem_cache_destroy(kmem_cache_t);
int kmem_cache_shrink(kmem_cache_t);
int kmem_cache_info(kmem_cache_t);
int kmem_cache_error(kmem_cache_t);
uint64 kmalloc(int);
int kfree(uint64);
int slab_write(uint64, const void*, int);
int slab_read(void*, uint64, int);

// ulib.c
int stat(const char*, struct stat*);
char* strcpy(char*, const char*);
void *memmove(void*, const void*, int);
char* strchr(const char*, char c);
int strcmp(const char*, const char*);
char* gets(char*, int max);
uint strlen(const char*);
void* memset(void*, int, uint);
int atoi(const char*);
int memcmp(const void *, const void *, uint);
void *memcpy(void *, const void *, uint);
char* sbrk(int);
char* sbrklazy(int);

// printf.c
void fprintf(int, const char*, ...) __attribute__ ((format (printf, 2, 3)));
void printf(const char*, ...) __attribute__ ((format (printf, 1, 2)));

// umalloc.c
void* malloc(uint);
void free(void*);
