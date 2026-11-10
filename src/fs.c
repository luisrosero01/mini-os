#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <pwd.h>
#include "kernel.h"
#include "seguridad.h"
#include "fs.h"

#define MAGIC       0x4D494E49 /* "MINI" */
#define MAX_REG     100
#define MAX_PATH    1024
#define BUFFER_SIZE 4096

typedef struct {
    unsigned int magic, version, total, usados, siguiente_id, limpio;
} Superbloque;

typedef struct {
    int  id, activo; /* activo = 0 -> ranura libre */
    char nombre[50], descripcion[100], propietario[32];
} Registro;

static int disco_fd = -1;
static Superbloque sb;
static char BASE_DIR[MAX_PATH];

/* ========================================================
   PARTE 1: DISCO VIRTUAL v3.0 (minios.disk + CRUD)
   ======================================================== */

static off_t pos_registro(int i) { /* acceso directo a la ranura i */
    return (off_t)sizeof(Superbloque) + (off_t)i * sizeof(Registro);
}

static int leer_registro(int i, Registro *r) {
    if (lseek(disco_fd, pos_registro(i), SEEK_SET) == (off_t)-1) return -1;
    return read(disco_fd, r, sizeof *r) == (ssize_t)sizeof *r ? 0 : -1;
}

static int escribir_registro(int i, const Registro *r) {
    if (lseek(disco_fd, pos_registro(i), SEEK_SET) == (off_t)-1) return -1;
    return write(disco_fd, r, sizeof *r) == (ssize_t)sizeof *r ? 0 : -1;
}

static void guardar_superbloque(void) {
    lseek(disco_fd, 0, SEEK_SET);
    if (write(disco_fd, &sb, sizeof sb) != (ssize_t)sizeof sb) {
        perror("write superbloque");
    }
    fsync(disco_fd);
}

static void inicializar_directorio_v2(void) {
    struct passwd *pw = getpwuid(getuid());
    const char *homedir = pw ? pw->pw_dir : ".";
    char miniso_dir[MAX_PATH];
    snprintf(miniso_dir, sizeof(miniso_dir), "%s/miniSO", homedir);
    mkdir(miniso_dir, 0777);
    snprintf(BASE_DIR, sizeof(BASE_DIR), "%s/miniSO/disk", homedir);
    mkdir(BASE_DIR, 0777);
}

int fs_montar(const char *ruta) {
    inicializar_directorio_v2();
    mkdir(DIR_DATOS, 0700);

    disco_fd = open(ruta, O_RDWR | O_CREAT, 0600);
    if (disco_fd < 0) {
        perror("open");
        return -1;
    }

    if (read(disco_fd, &sb, sizeof sb) != (ssize_t)sizeof sb || sb.magic != MAGIC) {
        printf("Disco sin formato: formateando...\n");
        memset(&sb, 0, sizeof sb);
        sb.magic = MAGIC;
        sb.version = 3;
        sb.total = MAX_REG;
        sb.usados = 0;
        sb.siguiente_id = 1;
        sb.limpio = 1;

        Registro vacio;
        memset(&vacio, 0, sizeof vacio);
        for (int i = 0; i < MAX_REG; i++) {
            escribir_registro(i, &vacio);
        }
    }

    if (!sb.limpio) {
        printf("ADVERTENCIA: el disco no se desmontó correctamente.\n");
    }

    sb.limpio = 0; /* en uso */
    guardar_superbloque();
    return 0;
}

void fs_desmontar(void) {
    if (disco_fd < 0) return;
    sb.limpio = 1;
    guardar_superbloque();
    close(disco_fd);
    disco_fd = -1;
}

void fs_crear(void) {
    if (sb.usados >= sb.total) {
        printf("Error: El disco virtual está lleno (%u/%u).\n", sb.usados, sb.total);
        return;
    }

    int ranura = -1;
    Registro r;
    for (int i = 0; i < (int)sb.total; i++) {
        if (leer_registro(i, &r) == 0 && r.activo == 0) {
            ranura = i;
            break;
        }
    }

    if (ranura < 0) {
        printf("Error: No hay ranuras libres.\n");
        return;
    }

    memset(&r, 0, sizeof r);
    leer_linea("Nombre del registro: ", r.nombre, sizeof r.nombre);
    if (r.nombre[0] == '\0') {
        printf("Cancelado: el nombre no puede estar vacío.\n");
        return;
    }
    leer_linea("Descripción: ", r.descripcion, sizeof r.descripcion);

    r.id = (int)sb.siguiente_id++;
    r.activo = 1;
    strncpy(r.propietario, seg_usuario_actual(), sizeof r.propietario - 1);

    if (escribir_registro(ranura, &r) == 0) {
        sb.usados++;
        guardar_superbloque();
        printf("Registro creado -> ID: %d | Ranura: #%d | Propietario: %s\n",
               r.id, ranura, r.propietario);
    }
}

void fs_listar(void) {
    printf("\n--- Registros en Disco Virtual (%u usados de %u) ---\n", sb.usados, sb.total);
    printf("%-7s | %-4s | %-20s | %-35s | %-12s\n",
           "Ranura", "ID", "Nombre", "Descripción", "Propietario");
    printf("--------------------------------------------------------------------------------------------\n");

    Registro r;
    int cont = 0;
    for (int i = 0; i < (int)sb.total; i++) {
        if (leer_registro(i, &r) == 0 && r.activo == 1) {
            printf("[#%02d]   | %-4d | %-20.20s | %-35.35s | %-12s\n",
                   i, r.id, r.nombre, r.descripcion, r.propietario);
            cont++;
        }
    }
    if (cont == 0) printf("  (Sin registros activos)\n");
}

void fs_actualizar(void) {
    int id = leer_entero("ID del registro a actualizar: ");
    if (id <= 0) {
        printf("ID inválido.\n");
        return;
    }

    Registro r;
    for (int i = 0; i < (int)sb.total; i++) {
        if (leer_registro(i, &r) == 0 && r.activo == 1 && r.id == id) {
            if (!seg_verificar_propietario(r.propietario)) return;

            printf("Actual [Ranura #%d]: %s - %s (Dueño: %s)\n",
                   i, r.nombre, r.descripcion, r.propietario);
            char nuevo_nom[50], nueva_desc[100];
            leer_linea("Nuevo nombre (Enter para conservar): ", nuevo_nom, sizeof nuevo_nom);
            leer_linea("Nueva descripción (Enter para conservar): ", nueva_desc, sizeof nueva_desc);

            if (nuevo_nom[0] != '\0') strncpy(r.nombre, nuevo_nom, sizeof r.nombre - 1);
            if (nueva_desc[0] != '\0') strncpy(r.descripcion, nueva_desc, sizeof r.descripcion - 1);

            if (escribir_registro(i, &r) == 0) {
                fsync(disco_fd);
                printf("Registro ID %d actualizado.\n", id);
            }
            return;
        }
    }
    printf("No existe registro activo con ID %d.\n", id);
}

void fs_borrar(void) {
    int id = leer_entero("ID del registro a borrar: ");
    if (id <= 0) {
        printf("ID inválido.\n");
        return;
    }

    Registro r;
    for (int i = 0; i < (int)sb.total; i++) {
        if (leer_registro(i, &r) == 0 && r.activo == 1 && r.id == id) {
            if (!seg_verificar_propietario(r.propietario)) return;

            r.activo = 0;
            if (escribir_registro(i, &r) == 0) {
                if (sb.usados > 0) sb.usados--;
                guardar_superbloque();
                printf("Registro ID %d borrado. Ranura #%d liberada.\n", id, i);
            }
            return;
        }
    }
    printf("No existe registro activo con ID %d.\n", id);
}

void fs_estado(void) {
    printf("\n================ ESTADO DEL DISCO VIRTUAL (minios.disk) ================\n");
    printf("  Número mágico : 0x%08X (\"MINI\")\n", sb.magic);
    printf("  Versión FS    : v%u\n", sb.version);
    printf("  Capacidad     : %u ranuras (%zu bytes por registro)\n", sb.total, sizeof(Registro));
    printf("  Ranuras usadas: %u\n", sb.usados);
    printf("  Ranuras libres: %u\n", sb.total - sb.usados);
    printf("  Siguiente ID  : %u\n", sb.siguiente_id);
    printf("  Estado        : %s\n", sb.limpio ? "LIMPIO (1)" : "EN USO / SUCIO (0)");
    printf("\n  Mapa de ocupación ([#] = usada, [.] = libre):\n  ");

    Registro r;
    for (int i = 0; i < (int)sb.total; i++) {
        int ocupado = (leer_registro(i, &r) == 0 && r.activo == 1);
        printf("%s", ocupado ? "[#]" : "[.]");
        if ((i + 1) % 20 == 0 && (i + 1) < (int)sb.total) printf("\n  ");
    }
    printf("\n========================================================================\n");
}

int fs_guardar_record(const char *jugador, int puntaje, int nivel) {
    if (disco_fd < 0 || sb.usados >= sb.total) return -1;
    int ranura = -1;
    Registro r;
    for (int i = 0; i < (int)sb.total; i++) {
        if (leer_registro(i, &r) == 0 && r.activo == 0) {
            ranura = i;
            break;
        }
    }
    if (ranura < 0) return -1;

    memset(&r, 0, sizeof r);
    r.id = (int)sb.siguiente_id++;
    r.activo = 1;
    snprintf(r.nombre, sizeof r.nombre, "RECORD SNAKE: %s", jugador);
    snprintf(r.descripcion, sizeof r.descripcion, "Puntaje: %d pts | Nivel: %d", puntaje, nivel);
    strncpy(r.propietario, jugador, sizeof r.propietario - 1);

    if (escribir_registro(ranura, &r) == 0) {
        sb.usados++;
        guardar_superbloque();
        return r.id;
    }
    return -1;
}

/* ========================================================
   PARTE 2: GESTOR DE ARCHIVOS DE v2.0 (10 OPCIONES)
   ======================================================== */

static int obtener_ruta_segura(const char *nombre_archivo, char *ruta_final) {
    if (nombre_archivo[0] == '\0') return 0;
    char ruta_temp[MAX_PATH];
    snprintf(ruta_temp, sizeof(ruta_temp), "%.700s/%.255s", BASE_DIR, nombre_archivo);

    char ruta_real[MAX_PATH];
    if (realpath(ruta_temp, ruta_real) != NULL) {
        if (strncmp(ruta_real, BASE_DIR, strlen(BASE_DIR)) != 0) {
            fprintf(stderr, "ERROR DE SEGURIDAD: Intento de salir del disco rechazado.\n");
            return 0;
        }
        strncpy(ruta_final, ruta_real, MAX_PATH - 1);
        ruta_final[MAX_PATH - 1] = '\0';
    } else {
        if (strstr(nombre_archivo, "../") != NULL) {
            fprintf(stderr, "ERROR DE SEGURIDAD: Ruta inválida.\n");
            return 0;
        }
        strncpy(ruta_final, ruta_temp, MAX_PATH - 1);
        ruta_final[MAX_PATH - 1] = '\0';
    }
    return 1;
}

static void listar_directorio_v2(void) {
    DIR *d = opendir(BASE_DIR);
    if (!d) { perror("Error opendir"); return; }

    struct dirent *dir;
    printf("\n%-20s | %-10s | %-10s | %-8s | %s\n",
           "NOMBRE", "TIPO", "TAMAÑO(B)", "PERMISOS", "ULT. MODIFICACION");
    printf("--------------------------------------------------------------------------------\n");
    while ((dir = readdir(d)) != NULL) {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;
        char ruta_completa[MAX_PATH + 256];
        snprintf(ruta_completa, sizeof(ruta_completa), "%s/%s", BASE_DIR, dir->d_name);
        struct stat st;
        if (stat(ruta_completa, &st) == 0) {
            char tiempo[64];
            strftime(tiempo, sizeof(tiempo), "%Y-%m-%d %H:%M", localtime(&st.st_mtime));
            printf("%-20s | %-10s | %-10ld | %04o     | %s\n",
                   dir->d_name, S_ISDIR(st.st_mode) ? "Directorio" : "Archivo",
                   (long)st.st_size, st.st_mode & 0777, tiempo);
        }
    }
    closedir(d);
}

static void crear_archivo_v2(void) {
    char nombre[256], ruta[MAX_PATH], contenido[BUFFER_SIZE];
    leer_linea("Nombre del nuevo archivo: ", nombre, sizeof nombre);
    if (!obtener_ruta_segura(nombre, ruta)) return;

    int fd = open(ruta, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) { perror("Error open"); return; }

    leer_linea("Escriba el contenido: ", contenido, sizeof contenido);
    size_t len = strlen(contenido);
    contenido[len] = '\n';
    if (write(fd, contenido, len + 1) < 0) perror("Error write");
    fsync(fd);
    close(fd);
    printf("Archivo guardado exitosamente.\n");
}

static void crear_directorio_v2(void) {
    char nombre[256], ruta[MAX_PATH];
    leer_linea("Nombre del nuevo directorio: ", nombre, sizeof nombre);
    if (!obtener_ruta_segura(nombre, ruta)) return;

    if (mkdir(ruta, 0755) == 0) printf("Directorio creado.\n");
    else perror("Error mkdir");
}

static void leer_archivo_v2(void) {
    char nombre[256], ruta[MAX_PATH];
    leer_linea("Archivo a leer: ", nombre, sizeof nombre);
    if (!obtener_ruta_segura(nombre, ruta)) return;

    int fd = open(ruta, O_RDONLY);
    if (fd < 0) { perror("Error open"); return; }

    char buffer[BUFFER_SIZE];
    ssize_t leidos = read(fd, buffer, sizeof(buffer));
    if (leidos < 0) { perror("Error read"); close(fd); return; }

    int es_binario = 0;
    for (ssize_t i = 0; i < leidos; i++) {
        if (buffer[i] == '\0') { es_binario = 1; break; }
    }

    if (!es_binario) {
        printf("\n--- CONTENIDO (TEXTO) ---\n");
        if (write(STDOUT_FILENO, buffer, (size_t)leidos) < 0) perror("write");
        while ((leidos = read(fd, buffer, sizeof(buffer))) > 0) {
            if (write(STDOUT_FILENO, buffer, (size_t)leidos) < 0) break;
        }
        printf("\n-------------------------\n");
    } else {
        struct stat st;
        stat(ruta, &st);
        printf("\n--- METADATOS (BINARIO) ---\n");
        printf("Tamaño: %ld bytes\nPrimeros 64 bytes (HEX):\n", (long)st.st_size);
        for (ssize_t i = 0; i < leidos && i < 64; i++) {
            printf("%02X ", (unsigned char)buffer[i]);
            if ((i + 1) % 8 == 0) printf("\n");
        }
        printf("\n---------------------------\n");
    }
    close(fd);
}

static void editar_archivo_v2(void) {
    char nombre[256], ruta[MAX_PATH], contenido[BUFFER_SIZE];
    leer_linea("Archivo a editar: ", nombre, sizeof nombre);
    if (!obtener_ruta_segura(nombre, ruta)) return;

    printf("1. Sobrescribir (Truncar)\n2. Agregar al final (Append)\n");
    int modo = leer_entero("Opción: ");
    if (modo != 1 && modo != 2) { printf("Modo inválido.\n"); return; }

    int flags = O_WRONLY | O_CREAT | ((modo == 1) ? O_TRUNC : O_APPEND);
    int fd = open(ruta, flags, 0644);
    if (fd < 0) { perror("Error open"); return; }

    leer_linea("Escriba el nuevo contenido: ", contenido, sizeof contenido);
    size_t len = strlen(contenido);
    contenido[len] = '\n';
    if (write(fd, contenido, len + 1) < 0) perror("Error write");
    fsync(fd);
    close(fd);
    printf("Archivo editado.\n");
}

static void copiar_archivo_v2(void) {
    char origen[256], destino[256], ruta_orig[MAX_PATH], ruta_dest[MAX_PATH];
    leer_linea("Archivo original: ", origen, sizeof origen);
    leer_linea("Nombre de la copia: ", destino, sizeof destino);
    if (!obtener_ruta_segura(origen, ruta_orig) || !obtener_ruta_segura(destino, ruta_dest)) return;

    int fd_orig = open(ruta_orig, O_RDONLY);
    if (fd_orig < 0) { perror("Error origen"); return; }
    int fd_dest = open(ruta_dest, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd_dest < 0) { perror("Error destino"); close(fd_orig); return; }

    char buffer[BUFFER_SIZE];
    ssize_t bytes_leidos;
    while ((bytes_leidos = read(fd_orig, buffer, BUFFER_SIZE)) > 0) {
        if (write(fd_dest, buffer, (size_t)bytes_leidos) < 0) break;
    }
    fsync(fd_dest);
    close(fd_orig);
    close(fd_dest);

    struct stat st1, st2;
    stat(ruta_orig, &st1);
    stat(ruta_dest, &st2);
    if (st1.st_size != st2.st_size) {
        printf("ERROR: Los tamaños no coinciden tras la copia.\n");
        return;
    }

    fd_orig = open(ruta_orig, O_RDONLY);
    fd_dest = open(ruta_dest, O_RDONLY);
    char buf1[BUFFER_SIZE], buf2[BUFFER_SIZE];
    int identicos = 1;
    while ((bytes_leidos = read(fd_orig, buf1, BUFFER_SIZE)) > 0) {
        if (read(fd_dest, buf2, (size_t)bytes_leidos) != bytes_leidos ||
            memcmp(buf1, buf2, (size_t)bytes_leidos) != 0) {
            identicos = 0;
            break;
        }
    }
    close(fd_orig);
    close(fd_dest);

    if (identicos) printf("Integridad verificada: La copia es exacta byte a byte.\n");
    else printf("ERROR: Los archivos difieren en contenido.\n");
}

static void renombrar_archivo_v2(void) {
    char viejo[256], nuevo[256], ruta_vieja[MAX_PATH], ruta_nueva[MAX_PATH];
    leer_linea("Nombre actual: ", viejo, sizeof viejo);
    leer_linea("Nuevo nombre: ", nuevo, sizeof nuevo);
    if (!obtener_ruta_segura(viejo, ruta_vieja) || !obtener_ruta_segura(nuevo, ruta_nueva)) return;

    if (rename(ruta_vieja, ruta_nueva) == 0) printf("Renombrado exitoso.\n");
    else perror("Error rename");
}

static void cambiar_permisos_v2(void) {
    char nombre[256], perm_str[16], ruta[MAX_PATH], *fin;
    leer_linea("Nombre del archivo: ", nombre, sizeof nombre);
    leer_linea("Nuevos permisos en octal (ej. 777 o 644): ", perm_str, sizeof perm_str);
    if (perm_str[0] == '\0') return;
    long modo = strtol(perm_str, &fin, 8);
    if (fin == perm_str || *fin != '\0') { printf("Permiso octal inválido.\n"); return; }

    if (!obtener_ruta_segura(nombre, ruta)) return;
    if (chmod(ruta, (mode_t)modo) == 0) printf("Permisos actualizados.\n");
    else perror("Error chmod");
}

static void buscar_archivo_v2(void) {
    char termino[256];
    leer_linea("Buscar (nombre/extensión): ", termino, sizeof termino);
    DIR *d = opendir(BASE_DIR);
    if (!d) return;

    struct dirent *dir;
    printf("\nResultados de la búsqueda:\n");
    int encontrados = 0;
    while ((dir = readdir(d)) != NULL) {
        if (strstr(dir->d_name, termino) != NULL) {
            printf("- %s\n", dir->d_name);
            encontrados++;
        }
    }
    if (encontrados == 0) printf("Ningún archivo coincide.\n");
    closedir(d);
}

static void eliminar_archivo_v2(void) {
    char nombre[256], ruta[MAX_PATH], conf[10];
    leer_linea("Nombre a eliminar: ", nombre, sizeof nombre);
    if (!obtener_ruta_segura(nombre, ruta)) return;

    leer_linea("¿Confirmar eliminación? (s/n): ", conf, sizeof conf);
    if (conf[0] == 's' || conf[0] == 'S') {
        struct stat st;
        if (stat(ruta, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                if (rmdir(ruta) == 0) printf("Directorio borrado.\n");
                else perror("Error rmdir");
            } else {
                if (unlink(ruta) == 0) printf("Archivo borrado.\n");
                else perror("Error unlink");
            }
        } else perror("Error stat");
    }
}

void fs_menu_archivos_v2(void) {
    while (1) {
        printf("\n=== GESTOR DE ARCHIVOS (v2.0 en ~/miniSO/disk) ===\n");
        printf("1. Listar  2. Crear Archivo  3. Crear Directorio  4. Leer\n");
        printf("5. Editar  6. Copiar (Byte)  7. Renombrar         8. Permisos\n");
        printf("9. Buscar  10. Eliminar      0. Volver\n");

        int opt = leer_entero("Opción: ");
        if (opt == 0 || feof(stdin)) break;

        switch (opt) {
            case 1:  if (seg_verificar(OBJ_REGISTROS, D_READ))   listar_directorio_v2(); break;
            case 2:  if (seg_verificar(OBJ_REGISTROS, D_CREATE)) crear_archivo_v2();     break;
            case 3:  if (seg_verificar(OBJ_REGISTROS, D_CREATE)) crear_directorio_v2();  break;
            case 4:  if (seg_verificar(OBJ_REGISTROS, D_READ))   leer_archivo_v2();      break;
            case 5:  if (seg_verificar(OBJ_REGISTROS, D_UPDATE)) editar_archivo_v2();    break;
            case 6:  if (seg_verificar(OBJ_REGISTROS, D_CREATE)) copiar_archivo_v2();    break;
            case 7:  if (seg_verificar(OBJ_REGISTROS, D_UPDATE)) renombrar_archivo_v2(); break;
            case 8:  if (seg_verificar(OBJ_REGISTROS, D_UPDATE)) cambiar_permisos_v2();  break;
            case 9:  if (seg_verificar(OBJ_REGISTROS, D_READ))   buscar_archivo_v2();    break;
            case 10: if (seg_verificar(OBJ_REGISTROS, D_DELETE)) eliminar_archivo_v2();  break;
            default: printf("Opción inválida.\n");
        }
    }
}
