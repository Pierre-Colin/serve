void init(int argc, char * const argv[])
#ifdef __GNUC__
__attribute__((nonnull (2)))
#endif
;

void cmdexec(void);

int getlistener(void)
#ifdef __GNUC__
__attribute__((const))
#endif
;
