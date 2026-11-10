#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "kernel.h"
#include "seguridad.h"

#define ARCH_MATRIZ DIR_DATOS "matriz.dat"

/* POLÍTICA POR DEFECTO: filas = dominios, columnas = objetos, celda = derechos */
static unsigned char matriz[NUM_ROLES][NUM_OBJETOS] = {
/*               registros                             juegos  partidas                        procesos                        matriz            sistema */
/* admin    */ { D_CREATE|D_READ|D_UPDATE|D_DELETE,    D_EXEC, D_CREATE|D_READ|D_UPDATE,       D_READ|D_UPDATE|D_DELETE,       D_READ|D_UPDATE,  D_EXEC },
/* operador */ { D_CREATE|D_READ|D_UPDATE,             D_EXEC, D_CREATE|D_READ|D_UPDATE,       D_READ|D_UPDATE|D_DELETE,       D_READ,           0      },
/* invitado */ { D_READ,                               D_EXEC, 0,                              D_READ,                         0,                0      },
};

static const char *NOMBRE_ROL[NUM_ROLES] = { "admin", "operador", "invitado" };
static const char *NOMBRE_OBJ[NUM_OBJETOS] = {
    "registros", "juegos", "partidas", "procesos", "matriz", "sistema"
};

static int rol_actual = -1; /* -1 = sin sesión */

void seg_iniciar_sesion(void) {
    int r;
    printf("\n--- Selección de Usuario / Rol ---\n");
    do {
        r = leer_entero("Rol (0=admin, 1=operador, 2=invitado): ");
        if (feof(stdin)) exit(0);
        if (r < 0 || r >= NUM_ROLES) {
            printf("Rol inválido. Intente de nuevo.\n");
        }
    } while (r < 0 || r >= NUM_ROLES);
    rol_actual = r;
    printf("Sesión iniciada como: %s\n", NOMBRE_ROL[rol_actual]);
}

const char *seg_usuario_actual(void) {
    return (rol_actual < 0 || rol_actual >= NUM_ROLES) ? "-" : NOMBRE_ROL[rol_actual];
}

int seg_es_admin(void) {
    return rol_actual == ROL_ADMIN;
}

/* MECANISMO: mediación completa + fallo seguro */
int seg_verificar(Objeto obj, unsigned char derecho) {
    if (rol_actual < 0 || rol_actual >= NUM_ROLES) {
        printf("  ACCESO DENEGADO (sin sesión activa)\n");
        return 0;
    }
    if (obj < 0 || obj >= NUM_OBJETOS) {
        printf("  ACCESO DENEGADO (objeto inválido)\n");
        return 0;
    }
    if ((matriz[rol_actual][obj] & derecho) == derecho) {
        return 1;
    }
    printf("  ACCESO DENEGADO\n");
    return 0;
}

/* Verificación de propiedad: el admin puede todo; el operador solo lo propio */
int seg_verificar_propietario(const char *propietario) {
    if (rol_actual < 0) {
        printf("  ACCESO DENEGADO\n");
        return 0;
    }
    if (rol_actual == ROL_ADMIN) return 1;
    if (propietario && strcmp(propietario, seg_usuario_actual()) == 0) {
        return 1;
    }
    printf("  ACCESO DENEGADO: el recurso pertenece a '%s'\n",
           propietario ? propietario : "otro usuario");
    return 0;
}

void seg_guardar_matriz(void) {
    mkdir(DIR_DATOS, 0700);
    int fd = open(ARCH_MATRIZ, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        perror("open matriz");
        return;
    }
    if (write(fd, matriz, sizeof matriz) != (ssize_t)sizeof matriz) {
        perror("write matriz");
    }
    fsync(fd);
    close(fd);
}

void seg_cargar_matriz(void) {
    mkdir(DIR_DATOS, 0700);
    int fd = open(ARCH_MATRIZ, O_RDONLY);
    if (fd < 0) {
        seg_guardar_matriz(); /* primera vez: guarda la de fábrica */
        return;
    }
    if (read(fd, matriz, sizeof matriz) != (ssize_t)sizeof matriz) {
        printf("matriz.dat dañada: se usan los valores por defecto\n");
    }
    /* Garantizar regla de seguridad: admin siempre conserva control de matriz */
    matriz[ROL_ADMIN][OBJ_MATRIZ] |= (D_READ | D_UPDATE);
    close(fd);
}

static void formatear_derechos(unsigned char d, char out[6]) {
    out[0] = (d & D_CREATE) ? 'C' : '-';
    out[1] = (d & D_READ)   ? 'R' : '-';
    out[2] = (d & D_UPDATE) ? 'U' : '-';
    out[3] = (d & D_DELETE) ? 'D' : '-';
    out[4] = (d & D_EXEC)   ? 'X' : '-';
    out[5] = '\0';
}

void seg_ver_matriz(void) {
    printf("\n================ MATRIZ DE ACCESO (C=Create R=Read U=Update D=Delete X=Exec) ================\n");
    printf("%-12s", "ROL \\ OBJ");
    for (int o = 0; o < NUM_OBJETOS; o++) {
        printf("| %-10s ", NOMBRE_OBJ[o]);
    }
    printf("\n---------------------------------------------------------------------------------------------\n");
    for (int r = 0; r < NUM_ROLES; r++) {
        printf("%-12s", NOMBRE_ROL[r]);
        for (int o = 0; o < NUM_OBJETOS; o++) {
            char perm[6];
            formatear_derechos(matriz[r][o], perm);
            printf("|   %s    ", perm);
        }
        printf("\n");
    }
    printf("=============================================================================================\n");
}

void seg_modificar_matriz(void) {
    seg_ver_matriz();
    printf("\nSeleccione el dominio (rol) a modificar:\n");
    for (int r = 0; r < NUM_ROLES; r++) {
        printf("  %d = %s\n", r, NOMBRE_ROL[r]);
    }
    int r = leer_entero("Dominio: ");
    if (r < 0 || r >= NUM_ROLES) {
        printf("Dominio inválido.\n");
        return;
    }

    printf("\nSeleccione el objeto:\n");
    for (int o = 0; o < NUM_OBJETOS; o++) {
        printf("  %d = %s\n", o, NOMBRE_OBJ[o]);
    }
    int o = leer_entero("Objeto: ");
    if (o < 0 || o >= NUM_OBJETOS) {
        printf("Objeto inválido.\n");
        return;
    }

    printf("\nIngrese los nuevos derechos sumando los valores en bits (0 para ninguno):\n");
    printf("  1 = Create (C)\n  2 = Read   (R)\n  4 = Update (U)\n  8 = Delete (D)\n 16 = Exec   (X)\n");
    printf("  (Ejemplo: R+U = 6, C+R+U = 7, C+R+U+D = 15)\n");
    int d = leer_entero("Nuevo valor de derechos (0-31): ");
    if (d < 0 || d > 31) {
        printf("Valor de derechos inválido.\n");
        return;
    }

    /* Regla: el admin no puede quitarse a sí mismo el control de la matriz ni apagar el sistema */
    if (r == ROL_ADMIN && o == OBJ_MATRIZ && ((d & (D_READ | D_UPDATE)) != (D_READ | D_UPDATE))) {
        printf("ERROR DE PROTECCIÓN: El admin no puede quitarse a sí mismo el control (R/U) de la matriz.\n");
        return;
    }
    if (r == ROL_ADMIN && o == OBJ_SISTEMA && !(d & D_EXEC)) {
        printf("ERROR DE PROTECCIÓN: El admin no puede quitarse el permiso de apagar el sistema.\n");
        return;
    }

    matriz[r][o] = (unsigned char)d;
    seg_guardar_matriz();
    printf("Matriz actualizada y guardada en disco exitosamente.\n");
    seg_ver_matriz();
}
