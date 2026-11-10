#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel.h"

void leer_linea(const char *prompt, char *buf, int n) {
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(buf, n, stdin)) {
        buf[0] = '\0';
        return; /* EOF: feof(stdin) queda en 1 */
    }
    buf[strcspn(buf, "\n")] = '\0';
}

int leer_entero(const char *prompt) {
    char buf[32], *fin;
    leer_linea(prompt, buf, sizeof buf);
    if (buf[0] == '\0') return -1;
    long v = strtol(buf, &fin, 10);
    return (fin == buf || *fin != '\0') ? -1 : (int)v; /* letras o vacío -> -1 */
}
