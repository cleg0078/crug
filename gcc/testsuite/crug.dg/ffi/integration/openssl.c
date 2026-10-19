/* OpenSSL: SSL_CTX_new allocates, SSL_CTX_free frees.  */

#include <stdio.h>


typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_method_st SSL_METHOD;

extern const SSL_METHOD *TLS_client_method (void);
extern SSL_CTX *SSL_CTX_new (const SSL_METHOD *method);
extern void SSL_CTX_free (SSL_CTX *ctx);

static int created;
static int freed;

SSL_CTX *my_ctx_new (void)
{
  SSL_CTX *ctx = SSL_CTX_new (TLS_client_method ());
  if (ctx != 0)
    created++;
  return ctx;
}

void my_ctx_free (SSL_CTX *ctx)
{
  if (ctx == 0)
    return;
  freed++;
  SSL_CTX_free (ctx);
}

int main (void)
{
  int ok;
  {
    own SSL_CTX *ctx = my_ctx_new ();
    ok = (ctx != 0);
  }
  if (!ok)
    return 1;
  printf ("ok openssl created=%d freed=%d\n", created, freed);
  return (created == 1 && freed == 1) ? 0 : 2;
}
