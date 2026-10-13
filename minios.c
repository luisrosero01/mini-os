#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <errno.h>
#include <pwd.h>
#include <ctype.h>

#define MAX_PATH 1024
#define BUFFER_SIZE 4096

char BASE_DIR[MAX_PATH];

/* ========================================================
   UTILIDADES Y SEGURIDAD
   ======================================================== */

void clear_input() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

void inicializar_disco() {
    struct passwd *pw = getpwuid(getuid());
    const char *homedir = pw->pw_dir;
    
    char miniso_dir[MAX_PATH];
    snprintf(miniso_dir, sizeof(miniso_dir), "%s/miniSO", homedir);
    mkdir(miniso_dir, 0777); 
    
    snprintf(BASE_DIR, sizeof(BASE_DIR), "%s/miniSO/disk", homedir);
    mkdir(BASE_DIR, 0777);   
}

// Aislamiento: Verifica que no se escape con ../../
int obtener_ruta_segura(const char *nombre_archivo, char *ruta_final) {
    char ruta_temp[MAX_PATH];
    snprintf(ruta_temp, sizeof(ruta_temp), "%s/%s", BASE_DIR, nombre_archivo);
    
    char ruta_real[MAX_PATH];
    if (realpath(ruta_temp, ruta_real) != NULL) {
        if (strncmp(ruta_real, BASE_DIR, strlen(BASE_DIR)) != 0) {
            fprintf(stderr, "ERROR DE SEGURIDAD: Intento de salir del disco rechazado.\n");
            return 0;
        }
        strcpy(ruta_final, ruta_real);
    } else {
        if (strstr(nombre_archivo, "../") != NULL) {
            fprintf(stderr, "ERROR DE SEGURIDAD: Ruta inválida.\n");
            return 0;
        }
        strcpy(ruta_final, ruta_temp);
    }
    return 1;
}

/* ========================================================
   GESTOR DE ARCHIVOS (10 OPCIONES)
   ======================================================== */

// 1. Listar
void listar_directorio() {
    DIR *d;
    struct dirent *dir;
    d = opendir(BASE_DIR);
    if (d) {
        printf("\n%-20s | %-10s | %-10s | %-8s | %s\n", "NOMBRE", "TIPO", "TAMAÑO(B)", "PERMISOS", "ULT. MODIFICACION");
        printf("--------------------------------------------------------------------------------\n");
        while ((dir = readdir(d)) != NULL) {
            if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;
            
            char ruta_completa[MAX_PATH];
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
    } else { perror("Error opendir"); }
}

// 2. Crear archivo de texto
void crear_archivo() {
    char nombre[256], ruta[MAX_PATH], contenido[BUFFER_SIZE];
    printf("Nombre del nuevo archivo: "); scanf("%255s", nombre); clear_input();
    if (!obtener_ruta_segura(nombre, ruta)) return;

    int fd = open(ruta, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) { perror("Error open"); return; }

    printf("Escriba el contenido (presione Enter para finalizar):\n");
    fgets(contenido, BUFFER_SIZE, stdin);
    write(fd, contenido, strlen(contenido));
    close(fd);
    printf("Archivo guardado exitosamente.\n");
}

// 3. Crear directorio
void crear_directorio() {
    char nombre[256], ruta[MAX_PATH];
    printf("Nombre del nuevo directorio: "); scanf("%255s", nombre); clear_input();
    if (!obtener_ruta_segura(nombre, ruta)) return;

    if (mkdir(ruta, 0755) == 0) printf("Directorio creado.\n");
    else perror("Error mkdir");
}

// 4. Leer archivo (Texto o Binario)
void leer_archivo() {
    char nombre[256], ruta[MAX_PATH];
    printf("Archivo a leer: "); scanf("%255s", nombre); clear_input();
    if (!obtener_ruta_segura(nombre, ruta)) return;

    int fd = open(ruta, O_RDONLY);
    if (fd < 0) { perror("Error open"); return; }

    char buffer[BUFFER_SIZE];
    ssize_t leidos = read(fd, buffer, sizeof(buffer));
    
    // Detector simple de binario (si contiene byte NULO)
    int es_binario = 0;
    for (ssize_t i = 0; i < leidos; i++) {
        if (buffer[i] == '\0') { es_binario = 1; break; }
    }

    if (!es_binario) {
        printf("\n--- CONTENIDO (TEXTO) ---\n");
        write(STDOUT_FILENO, buffer, leidos);
        while ((leidos = read(fd, buffer, sizeof(buffer))) > 0) {
            write(STDOUT_FILENO, buffer, leidos);
        }
        printf("\n-------------------------\n");
    } else {
        struct stat st;
        stat(ruta, &st);
        printf("\n--- METADATOS (BINARIO) ---\n");
        printf("Tamaño: %ld bytes\n", (long)st.st_size);
        printf("Primeros 64 bytes (HEX):\n");
        for (ssize_t i = 0; i < leidos && i < 64; i++) {
            printf("%02X ", (unsigned char)buffer[i]);
            if ((i + 1) % 8 == 0) printf("\n");
        }
        printf("\n---------------------------\n");
    }
    close(fd);
}

// 5. Editar archivo
void editar_archivo() {
    char nombre[256], ruta[MAX_PATH], contenido[BUFFER_SIZE];
    int modo;
    printf("Archivo a editar: "); scanf("%255s", nombre); clear_input();
    if (!obtener_ruta_segura(nombre, ruta)) return;

    printf("1. Sobrescribir (Truncar)\n2. Agregar al final (Append)\nOpción: ");
    if(scanf("%d", &modo) != 1) { clear_input(); return; }
    clear_input();

    int flags = O_WRONLY | O_CREAT;
    flags |= (modo == 1) ? O_TRUNC : O_APPEND;

    int fd = open(ruta, flags, 0644);
    if (fd < 0) { perror("Error open"); return; }

    printf("Escriba el nuevo contenido (Enter para guardar):\n");
    fgets(contenido, BUFFER_SIZE, stdin);
    write(fd, contenido, strlen(contenido));
    close(fd);
    printf("Archivo editado.\n");
}

// 6. Copiar archivo (Con verificación Byte a Byte)
void copiar_archivo() {
    char origen[256], destino[256], ruta_orig[MAX_PATH], ruta_dest[MAX_PATH];
    printf("Archivo original: "); scanf("%255s", origen);
    printf("Nombre de la copia: "); scanf("%255s", destino); clear_input();

    if (!obtener_ruta_segura(origen, ruta_orig) || !obtener_ruta_segura(destino, ruta_dest)) return;

    int fd_orig = open(ruta_orig, O_RDONLY);
    if (fd_orig < 0) { perror("Error origen"); return; }
    
    int fd_dest = open(ruta_dest, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd_dest < 0) { perror("Error destino"); close(fd_orig); return; }

    char buffer[BUFFER_SIZE];
    ssize_t bytes_leidos;
    while ((bytes_leidos = read(fd_orig, buffer, BUFFER_SIZE)) > 0) {
        write(fd_dest, buffer, bytes_leidos);
    }
    close(fd_orig); close(fd_dest);

    // Verificación de Integridad
    struct stat st1, st2;
    stat(ruta_orig, &st1); stat(ruta_dest, &st2);
    if (st1.st_size != st2.st_size) {
        printf("ERROR: Los tamaños no coinciden tras la copia.\n"); return;
    }

    fd_orig = open(ruta_orig, O_RDONLY); fd_dest = open(ruta_dest, O_RDONLY);
    char buf1[BUFFER_SIZE], buf2[BUFFER_SIZE];
    int identicos = 1;
    while ((bytes_leidos = read(fd_orig, buf1, BUFFER_SIZE)) > 0) {
        read(fd_dest, buf2, bytes_leidos);
        if (memcmp(buf1, buf2, bytes_leidos) != 0) { identicos = 0; break; }
    }
    close(fd_orig); close(fd_dest);

    if (identicos) printf("Integridad verificada: La copia es exacta byte a byte.\n");
    else printf("ERROR: Los archivos difieren en contenido.\n");
}

// 7. Renombrar / Mover
void renombrar_archivo() {
    char viejo[256], nuevo[256], ruta_vieja[MAX_PATH], ruta_nueva[MAX_PATH];
    printf("Nombre actual: "); scanf("%255s", viejo);
    printf("Nuevo nombre: "); scanf("%255s", nuevo); clear_input();
    if (!obtener_ruta_segura(viejo, ruta_vieja) || !obtener_ruta_segura(nuevo, ruta_nueva)) return;

    if (rename(ruta_vieja, ruta_nueva) == 0) printf("Renombrado exitoso.\n");
    else perror("Error rename");
}

// 8. Cambiar permisos
void cambiar_permisos() {
    char nombre[256], ruta[MAX_PATH];
    unsigned int modo;
    printf("Nombre del archivo: "); scanf("%255s", nombre);
    printf("Nuevos permisos (ej. 777 o 644): "); 
    if(scanf("%o", &modo) != 1) { clear_input(); return; }
    clear_input();
    
    if (!obtener_ruta_segura(nombre, ruta)) return;
    if (chmod(ruta, modo) == 0) printf("Permisos actualizados.\n");
    else perror("Error chmod");
}

// 9. Buscar
void buscar_archivo() {
    char termino[256];
    printf("Buscar (nombre/extensión): "); scanf("%255s", termino); clear_input();
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

// 10. Eliminar
void eliminar_archivo() {
    char nombre[256], ruta[MAX_PATH], conf[10];
    printf("Nombre a eliminar: "); scanf("%255s", nombre); clear_input();
    if (!obtener_ruta_segura(nombre, ruta)) return;

    printf("¿Confirmar eliminación de '%s'? (s/n): ", nombre);
    scanf("%9s", conf); clear_input();
    
    if (conf[0] == 's' || conf[0] == 'S') {
        struct stat st;
        if (stat(ruta, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                if (rmdir(ruta) == 0) printf("Directorio borrado.\n");
                else perror("Error rmdir (¿está vacío?)");
            } else {
                if (unlink(ruta) == 0) printf("Archivo borrado.\n");
                else perror("Error unlink");
            }
        } else { perror("Error stat"); }
    }
}

void menu_archivos() {
    int opt = -1;
    while (opt != 0) {
        printf("\n=== GESTOR DE ARCHIVOS ===\n");
        printf("1. Listar  2. Crear Archivo  3. Crear Directorio  4. Leer\n");
        printf("5. Editar  6. Copiar (Byte)  7. Renombrar         8. Permisos\n");
        printf("9. Buscar  10. Eliminar      0. Volver\nOpción: ");
        
        if (scanf("%d", &opt) != 1) { clear_input(); opt = -1; continue; }
        clear_input();
        switch (opt) {
            case 1: listar_directorio(); break;
            case 2: crear_archivo(); break;
            case 3: crear_directorio(); break;
            case 4: leer_archivo(); break;
            case 5: editar_archivo(); break;
            case 6: copiar_archivo(); break;
            case 7: renombrar_archivo(); break;
            case 8: cambiar_permisos(); break;
            case 9: buscar_archivo(); break;
            case 10: eliminar_archivo(); break;
            case 0: break;
            default: printf("Opción inválida.\n");
        }
    }
}

/* ========================================================
   CENTRO DE JUEGOS (PROCESOS)
   ======================================================== */

void lanzar_juego(const char *ruta, const char *nombre) {
    if (access(ruta, X_OK) != 0) {
        perror("ERROR: Juego no encontrado o sin permisos"); return;
    }

    struct termios orig_termios;
    tcgetattr(STDIN_FILENO, &orig_termios); // Guardar estado consola
    signal(SIGINT, SIG_IGN); // Proteger menú de Ctrl+C

    time_t inicio = time(NULL);
    printf("Iniciando %s...\n", nombre);

    pid_t pid = fork();
    if (pid < 0) {
        perror("Error fork");
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
        signal(SIGINT, SIG_DFL);
        return;
    } else if (pid == 0) {
        signal(SIGINT, SIG_DFL); // Devolver Ctrl+C al juego
        execl(ruta, nombre, (char *)NULL);
        perror("Error exec");
        _exit(127);
    } else {
        int status;
        waitpid(pid, &status, 0); // Bloqueo esperando al hijo
        double duracion = difftime(time(NULL), inicio);

        tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios); // Restaurar consola
        signal(SIGINT, SIG_DFL); // Restaurar Ctrl+C al menú

        printf("\n--- INFORME FINAL ---\nPID: %d | Duración: %.0f seg\n", pid, duracion);
        if (WIFEXITED(status)) printf("Cierre normal. Código: %d\n", WEXITSTATUS(status));
        else if (WIFSIGNALED(status)) printf("Terminado por señal: %d\n", WTERMSIG(status));
    }
}

void menu_juegos() {
    int opt = -1;
    while (opt != 0) {
        printf("\n=== JUEGOS ===\n1. Snake\n2. Tetris\n0. Volver\nOpción: ");
        if (scanf("%d", &opt) != 1) { clear_input(); opt = -1; continue; }
        clear_input();
        if (opt == 1) lanzar_juego("/usr/games/nsnake", "nsnake");
        else if (opt == 2) lanzar_juego("/usr/games/tetris-bsd", "tetris-bsd");
    }
}

/* ========================================================
   MAIN
   ======================================================== */

int main() {
    inicializar_disco();
    int opt = -1;
    while (opt != 0) {
        printf("\n========= MINI OS v2.0 =========\n");
        printf("1. Archivos\n2. Juegos\n3. Bitácora\n0. Apagar\nSeleccione: ");
        if (scanf("%d", &opt) != 1) { clear_input(); opt = -1; continue; }
        clear_input();
        
        switch (opt) {
            case 1: menu_archivos(); break;
            case 2: menu_juegos(); break;
            case 3: printf("Bitácora en progreso...\n"); break;
            case 0: printf("Apagando...\n"); break;
            default: printf("Inválido.\n");
        }
    }
    return 0;
}
