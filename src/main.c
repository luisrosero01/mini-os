#include <stdio.h>
#include "kernel.h"
#include "fs.h"
#include "seguridad.h"
#include "proc.h"
#include "juego.h"
#include "sistema.h"

static void mostrar_menu(void) {
    printf("\n============= MiniOS v%s | usuario: %s =============\n",
           VERSION_MINIOS, seg_usuario_actual());
    printf(" 1. Create (Disco virtual)       2. Read (Listar registros)\n");
    printf(" 3. Update (Editar registro)     4. Delete (Borrar registro)\n");
    printf(" 5. Estado del disco virtual     6. Gestor de Archivos (v2.0)\n");
    printf(" 7. Procesos (BSDGames)          8. Videojuego (Snake sesión)\n");
    printf(" 9. Ver matriz de acceso        10. Modificar matriz de acceso\n");
    printf("11. Acerca del sistema          12. Cambiar de usuario/rol\n");
    printf(" 0. Apagar\n");
    printf("=====================================================\n");
}

int main(void) {
    if (fs_montar(DIR_DATOS "minios.disk") != 0) return 1;
    seg_cargar_matriz();
    seg_iniciar_sesion();

    while (1) {
        mostrar_menu();
        switch (leer_entero(" Opción: ")) {
            case 1:
                if (seg_verificar(OBJ_REGISTROS, D_CREATE)) fs_crear();
                break;
            case 2:
                if (seg_verificar(OBJ_REGISTROS, D_READ)) fs_listar();
                break;
            case 3:
                if (seg_verificar(OBJ_REGISTROS, D_UPDATE)) fs_actualizar();
                break;
            case 4:
                if (seg_verificar(OBJ_REGISTROS, D_DELETE)) fs_borrar();
                break;
            case 5:
                if (seg_verificar(OBJ_REGISTROS, D_READ)) fs_estado();
                break;
            case 6:
                fs_menu_archivos_v2();
                break;
            case 7:
                proc_menu();
                break;
            case 8:
                juego_menu();
                break;
            case 9:
                if (seg_verificar(OBJ_MATRIZ, D_READ)) seg_ver_matriz();
                break;
            case 10:
                if (seg_verificar(OBJ_MATRIZ, D_UPDATE)) seg_modificar_matriz();
                break;
            case 11:
                acerca_de();
                break;
            case 12:
                seg_iniciar_sesion();
                break;
            case 0:
                if (seg_verificar(OBJ_SISTEMA, D_EXEC)) {
                    fs_desmontar();
                    printf("Apagando MiniOS...\n");
                    return 0;
                }
                break;
            default:
                if (feof(stdin)) {
                    fs_desmontar();
                    return 0; /* Ctrl+D */
                }
                printf("Opción inválida.\n");
        }
    }
}
