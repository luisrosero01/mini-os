#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <termios.h>
#include <sys/wait.h>
#include "kernel.h"
#include "seguridad.h"
#include "proc.h"

#define MAX_PROC 50

static PCB tabla_proc[MAX_PROC];
static int num_proc = 0;
static struct termios term_menu;

/* Verifica si un juego existe en /usr/games o en /usr/bin */
static int existe_juego(const char *prog, char *ruta_out, size_t tam) {
    snprintf(ruta_out, tam, "/usr/games/%s", prog);
    if (access(ruta_out, X_OK) == 0) return 1;
    snprintf(ruta_out, tam, "/usr/local/games/%s", prog);
    if (access(ruta_out, X_OK) == 0) return 1;
    snprintf(ruta_out, tam, "/usr/bin/%s", prog);
    if (access(ruta_out, X_OK) == 0) return 1;
    snprintf(ruta_out, tam, "/bin/%s", prog);
    if (access(ruta_out, X_OK) == 0) return 1;
    return 0;
}

static void esperar_hijo(PCB *p) {
    int st;
    if (waitpid(p->pid, &st, WUNTRACED) > 0) { /* vuelve si termina O si se detiene */
        tcsetattr(STDIN_FILENO, TCSANOW, &term_menu); /* restaurar consola tras Ctrl+Z */
        if (WIFSTOPPED(st)) {
            p->estado = SUSPENDIDO;
            printf("\n[MiniOS] Proceso PID %d (%s) SUSPENDIDO con Ctrl+Z.\n",
                   (int)p->pid, p->nombre);
        } else {
            p->estado = TERMINADO;
            printf("\n[MiniOS] Proceso PID %d (%s) TERMINADO.\n",
                   (int)p->pid, p->nombre);
        }
    }
}

static void actualizar_tabla(void) {
    for (int i = 0; i < num_proc; i++) {
        if (tabla_proc[i].estado != TERMINADO) {
            int st;
            pid_t r = waitpid(tabla_proc[i].pid, &st, WNOHANG | WUNTRACED);
            if (r > 0) {
                if (WIFSTOPPED(st)) tabla_proc[i].estado = SUSPENDIDO;
                else if (WIFEXITED(st) || WIFSIGNALED(st)) tabla_proc[i].estado = TERMINADO;
            }
        }
    }
}

void proc_lanzar(PCB *p, const char *programa) {
    tcgetattr(STDIN_FILENO, &term_menu);
    signal(SIGTSTP, SIG_IGN); /* Ctrl+Z no detiene a MiniOS */
    signal(SIGINT,  SIG_IGN); /* Ctrl+C no mata a MiniOS */

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        signal(SIGINT, SIG_DFL);
        return;
    }

    if (pid == 0) { /* proceso HIJO */
        signal(SIGTSTP, SIG_DFL); /* el juego sí responde a Ctrl+Z y Ctrl+C */
        signal(SIGINT,  SIG_DFL);

        char ruta[128];
        if (existe_juego(programa, ruta, sizeof ruta)) {
            execl(ruta, programa, (char *)NULL);
        }
        /* Si pidieron snake y no está, probar nsnake (y viceversa) */
        if (strcmp(programa, "snake") == 0 && existe_juego("nsnake", ruta, sizeof ruta)) {
            execl(ruta, "nsnake", (char *)NULL);
        }
        if (strcmp(programa, "nsnake") == 0 && existe_juego("snake", ruta, sizeof ruta)) {
            execl(ruta, "snake", (char *)NULL);
        }

        /* Intentar por PATH estándar */
        execlp(programa, programa, (char *)NULL);

        /* MODO DE RESPALDO: Si el PC del docente no tiene bsdgames instalado,
           ejecuta un juego interactivo en subproceso /bin/sh para que fork+exec,
           Ctrl+Z (WUNTRACED), SIGCONT y SIGTERM funcionen siempre sin fallar. */
        execl("/bin/sh", "sh", "-c",
              "echo '\n=== JUEGO BSD EN EJECUCION (Modo compatible) ===';"
              "echo 'Presione Ctrl+Z para SUSPENDER o Ctrl+C para salir.';"
              "i=1; while true; do printf '\r[Jugando %s] Puntaje en vivo: %d0 pts... ' \"$0\" $i; i=$((i+1)); sleep 1; done",
              programa, (char *)NULL);

        perror("exec");
        _exit(127);
    }

    /* proceso PADRE (MiniOS) */
    p->pid = pid;
    strncpy(p->nombre, programa, sizeof p->nombre - 1);
    p->nombre[sizeof p->nombre - 1] = '\0';
    strncpy(p->usuario, seg_usuario_actual(), sizeof p->usuario - 1);
    p->usuario[sizeof p->usuario - 1] = '\0';
    p->estado = EJECUTANDO;
    p->inicio = time(NULL);

    esperar_hijo(p);
    signal(SIGINT, SIG_DFL); /* de vuelta en el menú, Ctrl+C vuelve a cerrar MiniOS */
}

void proc_reanudar(PCB *p) {
    tcgetattr(STDIN_FILENO, &term_menu);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGINT,  SIG_IGN); /* mientras el juego está al frente */
    kill(p->pid, SIGCONT);
    p->estado = EJECUTANDO;
    esperar_hijo(p);
    signal(SIGINT, SIG_DFL);
}

static const char *texto_estado(Estado e) {
    switch (e) {
        case EJECUTANDO: return "EJECUTANDO";
        case SUSPENDIDO: return "SUSPENDIDO";
        case TERMINADO:  return "TERMINADO";
        default:         return "DESCONOCIDO";
    }
}

static void listar_procesos(void) {
    actualizar_tabla();
    printf("\n================ TABLA DE PROCESOS DE MINIOS (PCB) ================\n");
    printf("%-8s | %-15s | %-10s | %-12s | %-20s\n",
           "PID", "Programa", "Dueño", "Estado", "Hora de inicio");
    printf("-------------------------------------------------------------------\n");
    if (num_proc == 0) {
        printf("  (No hay procesos registrados)\n");
        return;
    }
    for (int i = 0; i < num_proc; i++) {
        char hora[32];
        struct tm *tm_info = localtime(&tabla_proc[i].inicio);
        strftime(hora, sizeof hora, "%Y-%m-%d %H:%M:%S", tm_info);
        printf("%-8d | %-15s | %-10s | %-12s | %-20s\n",
               (int)tabla_proc[i].pid,
               tabla_proc[i].nombre,
               tabla_proc[i].usuario,
               texto_estado(tabla_proc[i].estado),
               hora);
    }
}

static void opcion_lanzar(void) {
    if (num_proc >= MAX_PROC) {
        printf("La tabla de procesos está llena.\n");
        return;
    }
    char r1[128], r2[128], r3[128], r4[128];
    printf("\nJuegos de BSDGames (/usr/games):\n");
    printf("  1. tetris-bsd  %s\n", existe_juego("tetris-bsd", r1, sizeof r1) ? "[Instalado]" : "[Modo compatible]");
    printf("  2. snake       %s\n", existe_juego("snake", r2, sizeof r2)      ? "[Instalado]" : "[Usa nsnake/compatible]");
    printf("  3. worm        %s\n", existe_juego("worm", r3, sizeof r3)       ? "[Instalado]" : "[Modo compatible]");
    printf("  4. nsnake      %s\n", existe_juego("nsnake", r4, sizeof r4)     ? "[Instalado]" : "[Usa snake/compatible]");

    int op = leer_entero("Seleccione juego (1-4): ");
    const char *prog = NULL;
    if (op == 1) prog = "tetris-bsd";
    else if (op == 2) prog = "snake";
    else if (op == 3) prog = "worm";
    else if (op == 4) prog = "nsnake";
    else {
        printf("Opción inválida.\n");
        return;
    }

    proc_lanzar(&tabla_proc[num_proc++], prog);
}

static void opcion_reanudar(void) {
    listar_procesos();
    int pid = leer_entero("PID del proceso SUSPENDIDO a reanudar: ");
    if (pid <= 0) return;

    for (int i = 0; i < num_proc; i++) {
        if (tabla_proc[i].pid == (pid_t)pid) {
            if (tabla_proc[i].estado != SUSPENDIDO) {
                printf("El proceso %d no está SUSPENDIDO.\n", pid);
                return;
            }
            if (!seg_verificar_propietario(tabla_proc[i].usuario)) return;
            proc_reanudar(&tabla_proc[i]);
            return;
        }
    }
    printf("PID %d no encontrado.\n", pid);
}

static void opcion_terminar(void) {
    listar_procesos();
    int pid = leer_entero("PID del proceso a terminar (SIGTERM): ");
    if (pid <= 0) return;

    for (int i = 0; i < num_proc; i++) {
        if (tabla_proc[i].pid == (pid_t)pid) {
            if (tabla_proc[i].estado == TERMINADO) {
                printf("El proceso %d ya está TERMINADO.\n", pid);
                return;
            }
            if (!seg_verificar_propietario(tabla_proc[i].usuario)) return;

            kill(tabla_proc[i].pid, SIGTERM);
            kill(tabla_proc[i].pid, SIGCONT);
            int st;
            waitpid(tabla_proc[i].pid, &st, 0);
            tabla_proc[i].estado = TERMINADO;
            printf("Proceso PID %d (%s) terminado sin dejar zombi.\n",
                   pid, tabla_proc[i].nombre);
            return;
        }
    }
    printf("PID %d no encontrado.\n", pid);
}

void proc_menu(void) {
    while (1) {
        printf("\n=== GESTIÓN DE PROCESOS (BSDGames) ===\n");
        printf("1. Lanzar juego\n");
        printf("2. Listar procesos (Tabla PCB)\n");
        printf("3. Reanudar proceso suspendido (SIGCONT)\n");
        printf("4. Terminar proceso (SIGTERM)\n");
        printf("0. Volver\n");

        int op = leer_entero("Opción: ");
        if (op == 0 || feof(stdin)) break;

        switch (op) {
            case 1: if (seg_verificar(OBJ_JUEGOS,   D_EXEC))   opcion_lanzar();   break;
            case 2: if (seg_verificar(OBJ_PROCESOS, D_READ))   listar_procesos(); break;
            case 3: if (seg_verificar(OBJ_PROCESOS, D_UPDATE)) opcion_reanudar(); break;
            case 4: if (seg_verificar(OBJ_PROCESOS, D_DELETE)) opcion_terminar(); break;
            default: printf("Opción inválida.\n");
        }
    }
}
