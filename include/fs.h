#ifndef FS_H
#define FS_H

int  fs_montar(const char *ruta);
void fs_desmontar(void);
void fs_crear(void);
void fs_listar(void);
void fs_actualizar(void);
void fs_borrar(void);
void fs_estado(void);

/* Guardar récords del videojuego en el disco virtual */
int  fs_guardar_record(const char *jugador, int puntaje, int nivel);

/* Gestor de archivos de 10 opciones conservado de MiniOS v2.0 */
void fs_menu_archivos_v2(void);

#endif
