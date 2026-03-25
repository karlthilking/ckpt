#define _XOPEN_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <ucontext.h>
#include <ptrauth.h>

int main(void)
{
        ucontext_t uc;
        mcontext_t mc = uc.uc_mcontext;
        
        getcontext(&uc);
        printf("lr:\t\t%p\n", mc->__ss.__opaque_lr);

        mc->__ss.__opaque_lr = ptrauth_strip(&mc->__ss.__opaque_lr, 
                                             ptrauth_key_asib);
        printf("stripped lr:\t%p\n", mc->__ss.__opaque_lr);

        mc->__ss.__opaque_lr = ptrauth_sign_unauthenticated(&mc->__ss.__opaque_lr,
                                                            ptrauth_key_asib,
                                                            mc->__ss.__opaque_sp);
        printf("signed lr:\t%p\n", mc->__ss.__opaque_lr);

        exit(0);
}
