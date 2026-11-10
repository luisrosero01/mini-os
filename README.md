# MiniOS v3.0 — Taller 1 (Parte 1)

## 1. Integrantes y Compilación

### Integrantes:
- Integrante 1: [Escribir Nombre]
- Integrante 2: [Escribir Nombre]

### Cómo compilar y ejecutar:
El proyecto incluye un Makefile que compila todos los módulos con las banderas -Wall -Wextra sin generar advertencias:

    make clean && make
    ./minios

Para limpiar los binarios y objetos generados (build/ y minios):

    make clean

---

## 2. Diagrama de Capas de MiniOS v3.0

La arquitectura está organizada por capas estrictas, donde cada capa únicamente invoca funciones de las capas inferiores:

    +-------------------------------------------------------------------+
    | CAPA 4: INTERFAZ (src/main.c, src/util.c)                         |
    |  - Arranque, inicio de sesión y bucle infinito robusto del menú.  |
    |  - Nunca llama a open, read, write ni fork directamente.          |
    +-------------------------------------------------------------------+
                     |                                 |
                     v                                 |
    +-----------------------------------------+        |
    | CAPA 3: SERVICIOS                       |        |
    |  - src/proc.c    (PCB y BSDGames)       |        |
    |  - src/juego.c   (Snake con sesión)     |        |
    |  - src/sistema.c (Acerca del sistema)   |        |
    +-----------------------------------------+        |
                     |                                 v
                     +------------------> +-----------------------------+
                     |                    | CAPA 2: PROTECCIÓN          |
                     |                    |  - src/seguridad.c          |
                     |                    |  - Roles, objetos, derechos |
                     |                    |  - Matriz en matriz.dat     |
                     |                    +-----------------------------+
                     v                                 |
    +--------------------------------------------------+----------------+
    | CAPA 1: SISTEMA DE ARCHIVOS (src/fs.c)                            |
    |  - Disco virtual (datos/minios.disk) con Superbloque y CRUD.      |
    |  - Acceso directo con lseek, montaje/desmontaje y Gestor v2.0.    |
    +-------------------------------------------------------------------+
                                     |
                                     v
    +-------------------------------------------------------------------+
    | CAPA 0: KERNEL LINUX (Llamadas al Sistema POSIX)                  |
    |  - open, read, write, lseek, fsync, close                         |
    |  - fork, execlp, execl, waitpid, kill, signal, sigaction, uname   |
    +-------------------------------------------------------------------+

---

## 3. Traza de Llamadas al Sistema (strace -c ./minios)

Tabla obtenida al ejecutar strace -c ./minios, iniciar sesión, realizar un Create, luego un Read sobre el disco virtual y apagar el sistema:

    % time     seconds  usecs/call     calls    errors syscall
    ------ ----------- ----------- --------- --------- ----------------
     38.58    0.001274         424         3           fsync
     15.54    0.000513           4       113           read
     13.08    0.000432         432         1         1 access
      7.27    0.000240           5        46           write
      5.45    0.000180           1       106           lseek
      4.42    0.000146          18         8           mmap
      2.73    0.000090          90         1           set_tid_address
      2.73    0.000090          15         6           openat
      1.64    0.000054           6         8           close
      1.48    0.000049          16         3           mprotect
      1.42    0.000047           6         7           fstat
      1.15    0.000038          19         2         2 connect
      1.00    0.000033           8         4         4 mkdir
      0.88    0.000029          29         1           munmap
      0.73    0.000024          12         2           socket
      0.42    0.000014           4         3           brk
      0.36    0.000012           6         2           newfstatat
      0.30    0.000010           5         2           pread64
      0.15    0.000005           5         1           arch_prctl
      0.15    0.000005           5         1           set_robust_list
      0.15    0.000005           5         1           getrandom
      0.15    0.000005           5         1           rseq
      0.12    0.000004           4         1           prlimit64
      0.09    0.000003           3         1           getuid
      0.00    0.000000           0         1           execve
    ------ ----------- ----------- --------- --------- ----------------
    100.00    0.003302          10       325         7 total

### Explicación breve:
- openat (6 llamadas) y close (8 llamadas): Abren y cierran los descriptores de archivo del disco virtual (datos/minios.disk) y de la matriz de acceso (datos/matriz.dat) directamente en el kernel sin usar FILE*.
- lseek (106 llamadas): Desplaza el puntero del archivo de forma directa hacia el superbloque (offset 0) o hacia cada ranura i (sizeof(Superbloque) + i * sizeof(Registro)) durante el Create y el Read.
- read (113 llamadas) y write (46 llamadas): Leen las estructuras fijas de las ranuras del disco virtual, reciben la entrada estándar del usuario y escriben el nuevo registro y el superbloque.
- fsync (3 llamadas, 38.58% del tiempo): Vuelca los buffers en RAM hacia el almacenamiento físico en tres momentos críticos: al montar el disco (limpio = 0), al guardar el superbloque tras el Create y al desmontar (limpio = 1).

---

## 4. Semejanza con los Sistemas Operativos de la Historia

MiniOS v3.0 se asemeja principalmente a los sistemas operativos de Tiempo Compartido (Time-Sharing) de finales de los años 60 y años 70, tales como MULTICS y las primeras versiones de UNIX, combinados con la interacción por consola de los primeros sistemas de Computador Personal (PC). Se parece a MULTICS y UNIX temprano porque introduce multiusuario por dominios/roles con protección basada en una Matriz de Acceso (modelo de Lampson) donde cada recurso verifica permisos y propiedad antes de actuar; además, gestiona procesos concurrentes mediante fork y exec, llevando una tabla de bloques de control de procesos (PCB) y controlando cambios de estado (EJECUTANDO, SUSPENDIDO, TERMINADO) mediante señales (SIGTSTP, SIGCONT, SIGTERM), y organiza su almacenamiento secundario con un Superbloque que verifica integridad mediante un número mágico y una bandera de montaje limpio/sucio.

---

## 5. Matriz de Acceso y Justificación de Protección

| Dominio (Rol) \ Objeto | registros | juegos | partidas | procesos | matriz | sistema (Apagar) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| 0: admin    | CRUD- | ----X | CRU-- | -RUD- | -RU-- | ----X |
| 1: operador | CRU-- | ----X | CRU-- | -RUD- | -R--- | ----- |
| 2: invitado | -R--- | ----X | ----- | -R--- | ----- | ----- |

### Justificación celda por celda con Principios de Protección:

1. registros (Disco Virtual y Archivos):
   - admin (CRUD-): Separación de privilegios. Único rol con permiso total, incluyendo borrado (D_DELETE) administrativo.
   - operador (CRU--): Mínimo privilegio y Propiedad. Crea, lista y actualiza únicamente los registros propios, sin permiso de borrado por defecto.
   - invitado (-R---): Mínimo privilegio. Solo lectura para consultar registros sin alterar el disco virtual.

2. juegos (Lanzamiento de BSDGames y Snake):
   - admin, operador, invitado (----X): Mínimo privilegio. Al ser programas ejecutables, solo necesitan el derecho de ejecución (D_EXEC).

3. partidas (Sesiones guardadas partida_<usuario>.sav):
   - admin y operador (CRU--): Mínimo privilegio y Propiedad. Guardan (C/U) y cargan (R) únicamente su propio archivo de sesión en disco.
   - invitado (-----): Fallo seguro. Por ser una cuenta temporal, no puede crear ni cargar estados persistentes en disco.

4. procesos (Tabla PCB, suspender, reanudar y terminar):
   - admin (-RUD-): Separación de privilegios. Puede listar (R), reanudar (U) y terminar (D) cualquier proceso del sistema.
   - operador (-RUD- con validación de dueño): Mediación completa y Propiedad. Controla exclusivamente procesos iniciados por él mismo.
   - invitado (-R---): Mínimo privilegio. Solo observa la tabla de procesos (R), sin permiso para enviar señales.

5. matriz (Archivo matriz.dat y configuración de permisos):
   - admin (-RU--): Separación de privilegios y Fallo seguro. Único rol que puede ver y modificar la política de acceso, sin poder quitarse a sí mismo el control (R/U).
   - operador (-R---): Mínimo privilegio. Solo lectura para auditar sus permisos actuales.
   - invitado (-----): Fallo seguro. Se niega todo acceso a la configuración de seguridad.

6. sistema (Desmontaje del disco virtual y apagado):
   - admin (----X): Separación de privilegios. Apagar el sistema y desmontar el disco es una acción crítica exclusiva del administrador.
   - operador e invitado (-----): Fallo seguro. Ningún usuario no administrador puede apagar MiniOS.
