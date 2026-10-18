__thread int tls_counter;
_Thread_local int c11_counter;

void bump_tls(void) { tls_counter++; c11_counter++; }
int read_tls(void) { return tls_counter + c11_counter; }
