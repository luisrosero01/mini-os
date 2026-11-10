#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/stat.h>
#include "kernel.h"
#include "seguridad.h"
#include "fs.h"
#include "juego.h"

#define ANCHO 30
#define ALTO  14

#define DIR_ARRIBA    0
#define DIR_DERECHA   1
#define DIR_ABAJO     2
#define DIR_IZQUIERDA 3

static struct termios original;
static volatile sig_atomic_t autoguardar_flag = 0;

static void manejador_ctrl_c(int sig) {
    (void)sig;
    autoguardar_flag = 1;
}

static void modo_juego(int activar) {
    if (activar) {
        struct termios t;
        tcgetattr(STDIN_FILENO, &original);
        t = original;
        t.c_lflag &= ~(ICANON | ECHO); /* sin esperar Enter y sin eco */
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
    } else {
        tcsetattr(STDIN_FILENO, TCSANOW, &original); /* SIEMPRE restaurar */
    }
}

/* Devuelve la tecla presionada, o -1 si no se presionó nada en 'ms' milisegundos */
static int tecla(int ms) {
    fd_set fds;
    struct timeval tv = { 0, ms * 1000 };
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
        char c;
        if (read(STDIN_FILENO, &c, 1) == 1) return (unsigned char)c;
    }
    return -1;
}

int guardar_partida(const Partida *p) {
    mkdir(DIR_DATOS, 0700);
    char ruta[64];
    snprintf(ruta, sizeof ruta, "datos/partida_%s.sav", p->jugador);
    int fd = open(ruta, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    ssize_t n = write(fd, p, sizeof *p);
    fsync(fd); /* que llegue al disco */
    close(fd);
    return n == (ssize_t)sizeof *p ? 0 : -1;
}

int cargar_partida(const char *jugador, Partida *p) {
    char ruta[64];
    snprintf(ruta, sizeof ruta, "datos/partida_%s.sav", jugador);
    int fd = open(ruta, O_RDONLY);
    if (fd < 0) return -1; /* no tiene partida guardada */
    ssize_t n = read(fd, p, sizeof *p);
    close(fd);
    return n == (ssize_t)sizeof *p ? 0 : -1;
}

static void eliminar_partida(const char *jugador) {
    char ruta[64];
    snprintf(ruta, sizeof ruta, "datos/partida_%s.sav", jugador);
    unlink(ruta);
}

static void ubicar_comida(Partida *p) {
    while (1) {
        int cx = 1 + rand() % (ANCHO - 2);
        int cy = 1 + rand() % (ALTO - 2);
        int ocupado = 0;
        for (int i = 0; i < p->largo; i++) {
            if (p->cuerpo[i].x == cx && p->cuerpo[i].y == cy) {
                ocupado = 1;
                break;
            }
        }
        if (!ocupado) {
            p->comida.x = cx;
            p->comida.y = cy;
            return;
        }
    }
}

static void nueva_estructura_partida(Partida *p, const char *jugador) {
    memset(p, 0, sizeof *p);
    strncpy(p->jugador, jugador, sizeof p->jugador - 1);
    p->puntaje = 0;
    p->nivel = 1;
    p->direccion = DIR_DERECHA;
    p->largo = 4;
    int cx = ANCHO / 2;
    int cy = ALTO / 2;
    for (int i = 0; i < p->largo; i++) {
        p->cuerpo[i].x = cx - i;
        p->cuerpo[i].y = cy;
    }
    srand((unsigned int)time(NULL));
    ubicar_comida(p);
    p->guardada_en = (long)time(NULL);
}

static void dibujar(const Partida *p, const char *aviso) {
    printf("\033[H\033[J"); /* limpia la pantalla */
    printf("=== SNAKE MiniOS v3.0 | Usuario: %s | Puntaje: %d | Nivel: %d ===\n",
           p->jugador, p->puntaje, p->nivel);
    printf("Teclas: [W/A/S/D] Mover | [G] Guardar | [P] Guardar y salir | [Ctrl+C] Autoguardar\n");

    for (int y = 0; y < ALTO; y++) {
        for (int x = 0; x < ANCHO; x++) {
            if (y == 0 || y == ALTO - 1 || x == 0 || x == ANCHO - 1) {
                putchar('#');
            } else if (x == p->comida.x && y == p->comida.y) {
                putchar('@');
            } else {
                int es_cuerpo = 0;
                for (int i = 0; i < p->largo; i++) {
                    if (p->cuerpo[i].x == x && p->cuerpo[i].y == y) {
                        putchar(i == 0 ? 'O' : 'o');
                        es_cuerpo = 1;
                        break;
                    }
                }
                if (!es_cuerpo) putchar(' ');
            }
        }
        putchar('\n');
    }
    if (aviso && aviso[0] != '\0') printf(">> %s\n", aviso);
    fflush(stdout);
}

static void jugar_snake(Partida *p) {
    autoguardar_flag = 0;
    struct sigaction sa_new, sa_old;
    memset(&sa_new, 0, sizeof sa_new);
    sa_new.sa_handler = manejador_ctrl_c;
    sigaction(SIGINT, &sa_new, &sa_old);

    modo_juego(1);
    char aviso[80] = "En juego...";
    int activo = 1;

    while (activo) {
        dibujar(p, aviso);
        int ms = 200 - (p->nivel - 1) * 20;
        if (ms < 70) ms = 70;

        int c = tecla(ms);
        if (autoguardar_flag) {
            p->guardada_en = (long)time(NULL);
            guardar_partida(p);
            modo_juego(0);
            sigaction(SIGINT, &sa_old, NULL);
            printf("\n[Autoguardado Ctrl+C] Sesión guardada en datos/partida_%s.sav\n", p->jugador);
            return;
        }

        if (c == 'w' || c == 'W')      { if (p->direccion != DIR_ABAJO)     p->direccion = DIR_ARRIBA; }
        else if (c == 'd' || c == 'D') { if (p->direccion != DIR_IZQUIERDA) p->direccion = DIR_DERECHA; }
        else if (c == 's' || c == 'S') { if (p->direccion != DIR_ARRIBA)    p->direccion = DIR_ABAJO; }
        else if (c == 'a' || c == 'A') { if (p->direccion != DIR_DERECHA)   p->direccion = DIR_IZQUIERDA; }
        else if (c == 'g' || c == 'G') {
            if (seg_verificar(OBJ_PARTIDAS, D_CREATE)) {
                p->guardada_en = (long)time(NULL);
                if (guardar_partida(p) == 0) {
                    snprintf(aviso, sizeof aviso, "Partida guardada en datos/partida_%s.sav", p->jugador);
                }
            } else {
                snprintf(aviso, sizeof aviso, "ACCESO DENEGADO para guardar partida");
            }
            continue;
        } else if (c == 'p' || c == 'P' || c == 'q' || c == 'Q') {
            if (seg_verificar(OBJ_PARTIDAS, D_CREATE)) {
                p->guardada_en = (long)time(NULL);
                guardar_partida(p);
                modo_juego(0);
                sigaction(SIGINT, &sa_old, NULL);
                printf("\nPartida guardada en datos/partida_%s.sav. Volviendo al menú...\n", p->jugador);
                return;
            } else {
                modo_juego(0);
                sigaction(SIGINT, &sa_old, NULL);
                printf("\nSaliendo del juego sin guardar (rol sin permiso sobre partidas)...\n");
                return;
            }
        }

        Punto cabeza = p->cuerpo[0];
        if (p->direccion == DIR_ARRIBA)         cabeza.y--;
        else if (p->direccion == DIR_DERECHA)   cabeza.x++;
        else if (p->direccion == DIR_ABAJO)     cabeza.y++;
        else if (p->direccion == DIR_IZQUIERDA) cabeza.x--;

        if (cabeza.x <= 0 || cabeza.x >= ANCHO - 1 || cabeza.y <= 0 || cabeza.y >= ALTO - 1) {
            activo = 0;
            break;
        }
        for (int i = 0; i < p->largo; i++) {
            if (p->cuerpo[i].x == cabeza.x && p->cuerpo[i].y == cabeza.y) {
                activo = 0;
                break;
            }
        }
        if (!activo) break;

        if (cabeza.x == p->comida.x && cabeza.y == p->comida.y) {
            if (p->largo < MAX_COLA) p->largo++;
            for (int i = p->largo - 1; i > 0; i--) p->cuerpo[i] = p->cuerpo[i - 1];
            p->cuerpo[0] = cabeza;
            p->puntaje += 10;
            p->nivel = 1 + (p->puntaje / 30);
            ubicar_comida(p);
            snprintf(aviso, sizeof aviso, "¡+10 puntos!");
        } else {
            for (int i = p->largo - 1; i > 0; i--) p->cuerpo[i] = p->cuerpo[i - 1];
            p->cuerpo[0] = cabeza;
        }
    }

    modo_juego(0);
    sigaction(SIGINT, &sa_old, NULL);

    printf("\n=== FIN DEL JUEGO ===\nJugador: %s | Puntaje final: %d | Nivel: %d\n",
           p->jugador, p->puntaje, p->nivel);
    int rec_id = fs_guardar_record(p->jugador, p->puntaje, p->nivel);
    if (rec_id > 0) {
        printf("Récord guardado en el disco virtual (minios.disk) con ID #%d.\n", rec_id);
    }
    eliminar_partida(p->jugador);
}

void juego_menu(void) {
    while (1) {
        printf("\n=== VIDEOJUEGO CON SESIÓN GUARDADA (Snake) ===\n");
        printf("1. Nueva partida\n");
        printf("2. Continuar partida\n");
        printf("3. Ver récords guardados en el disco virtual\n");
        printf("0. Volver\n");

        int op = leer_entero("Opción: ");
        if (op == 0 || feof(stdin)) break;

        Partida p;
        switch (op) {
            case 1:
                if (seg_verificar(OBJ_JUEGOS, D_EXEC)) {
                    nueva_estructura_partida(&p, seg_usuario_actual());
                    jugar_snake(&p);
                }
                break;
            case 2:
                if (seg_verificar(OBJ_PARTIDAS, D_READ)) {
                    if (cargar_partida(seg_usuario_actual(), &p) == 0) {
                        if (!seg_verificar_propietario(p.jugador)) break;
                        jugar_snake(&p);
                    } else {
                        printf("No hay partida guardada para el usuario '%s'.\n",
                               seg_usuario_actual());
                    }
                }
                break;
            case 3:
                if (seg_verificar(OBJ_REGISTROS, D_READ)) fs_listar();
                break;
            default:
                printf("Opción inválida.\n");
        }
    }
}
