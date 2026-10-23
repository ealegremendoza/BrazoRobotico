#ifndef PULSADOR_CORE_H
#define PULSADOR_CORE_H

/*
 * Lógica pura del pulsador: no sabe de GPIO, timers ni locks.
 * Se copia tal cual al driver de Linux.
 *
 *   ISR de flanco   -> pul_edge()    anota el instante del último flanco
 *   timer periódico -> pul_update()  confirma el estado ya estable
 *   fop read        -> pul_format()  estado -> "0\n" / "1\n"
 *   fop write       -> pul_parse()   "0" / "1" -> estado del LED
 */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Estado del antirrebote (lo comparten la ISR y el timer: proteger con lock) */
struct pul_deb {
	uint64_t edge_us;   /* instante del último flanco */
	uint8_t  pending;   /* 1 si hubo flancos sin confirmar */
	uint8_t  state;     /* estado confirmado: 1 = pulsado */
};

void pul_init(struct pul_deb *d, int pressed);
void pul_edge(struct pul_deb *d, uint64_t now_us);
int  pul_update(struct pul_deb *d, int pressed, uint64_t now_us,
		uint32_t debounce_us);
int  pul_format(int pressed, char *buf, size_t len);
int  pul_parse(const char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif
