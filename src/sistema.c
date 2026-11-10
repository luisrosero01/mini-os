#include <stdio.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/utsname.h>
#include "kernel.h"
#include "seguridad.h"
#include "sistema.h"

void acerca_de(void) {
    struct utsname u;
    uname(&u);
    struct passwd *pw = getpwuid(getuid());
    const char *usuario_linux = pw ? pw->pw_name : "kali";

    printf("\n================ ACERCA DEL SISTEMA ================\n");
    printf("MiniOS v%s sobre %s %s (%s)\n",
           VERSION_MINIOS, u.sysname, u.release, u.machine);
    printf("Núcleos: %ld | PID: %d | Usuario Linux: %s (UID: %d)\n",
           sysconf(_SC_NPROCESSORS_ONLN), (int)getpid(), usuario_linux, (int)getuid());
    printf("Rol activo en MiniOS: %s\n", seg_usuario_actual());
    printf("====================================================\n");
}
