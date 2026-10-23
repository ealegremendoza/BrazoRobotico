#include <errno.h>
#include "pulsador_core.h"

/*
 * Antirrebote por "silencio": cada flanco reinicia la cuenta, y el estado
 * solo se confirma cuando pasaron debounce_us sin flancos. Recién ahí se
 * lee el pin, que ya está quieto.
 *
 *   pin      ‾‾‾‾‾|_|‾|_____________________|‾|_|‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
 *   flancos       ^ ^ ^                     ^ ^ ^
 *                     |<- debounce ->|          |<- debounce ->|
 *   state    0000000000000000000000001111111111111111111111111100
 *
 * (pin activo en bajo: state = 1 es pulsado, pin = 0)
 */

/**
 * @brief Inicializa el antirrebote con un estado ya conocido.
 *
 * @param d       Estado del antirrebote.
 * @param pressed Estado inicial del pulsador: 1 = pulsado, 0 = suelto.
 */
void pul_init(struct pul_deb *d, int pressed)
{
	d->edge_us = 0;
	d->pending = 0;
	d->state = pressed ? 1 : 0;
}

/**
 * @brief Registra un flanco: reinicia la ventana de antirrebote.
 *
 * Pensada para llamarse desde la ISR, bajo lock. No lee el pin: durante
 * el rebote su nivel no significa nada.
 *
 * @param d      Estado del antirrebote.
 * @param now_us Instante del flanco.
 */
void pul_edge(struct pul_deb *d, uint64_t now_us)
{
	d->edge_us = now_us;
	d->pending = 1;
}

/**
 * @brief Confirma el estado si pasó la ventana sin flancos.
 *
 * Pensada para llamarse desde el timer, bajo lock. Si hay flancos
 * pendientes y desde el último pasaron al menos debounce_us, toma pressed
 * como el nuevo estado estable.
 *
 * @param d           Estado del antirrebote.
 * @param pressed     Nivel lógico actual del pin: 1 = pulsado, 0 = suelto.
 * @param now_us      Instante actual.
 * @param debounce_us Tiempo sin flancos para dar el estado por estable.
 * @return El estado confirmado (1 = pulsado, 0 = suelto).
 */
int pul_update(struct pul_deb *d, int pressed, uint64_t now_us,
	       uint32_t debounce_us)
{
	/* now_us < edge_us: el flanco llegó después de leer el reloj */
	if (d->pending && now_us >= d->edge_us &&
	    now_us - d->edge_us >= debounce_us) {
		d->state = pressed ? 1 : 0;
		d->pending = 0;
	}
	return d->state;
}

/**
 * @brief Convierte el estado del pulsador en texto: "1\n" o "0\n".
 *
 * @param pressed Estado: distinto de 0 = pulsado.
 * @param buf     Buffer de destino (no se termina en '\0').
 * @param len     Tamaño de buf.
 * @return 2 (bytes escritos); -EINVAL si buf no alcanza.
 */
int pul_format(int pressed, char *buf, size_t len)
{
	if (len < 2)
		return -EINVAL;
	buf[0] = pressed ? '1' : '0';
	buf[1] = '\n';
	return 2;
}

/**
 * @brief Interpreta el texto escrito en el dispositivo: "0" o "1".
 *
 * Ignora '\n' y '\r' finales (Serial Monitor, echo).
 *
 * @param buf Texto recibido; no necesita terminar en '\0'.
 * @param len Cantidad de bytes de buf.
 * @return 0 o 1 (estado pedido para el LED); -EINVAL si, sin los '\n'/'\r'
 *         finales, no queda exactamente un '0' o un '1'.
 */
int pul_parse(const char *buf, size_t len)
{
	while (len && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
		len--;
	if (len != 1 || (buf[0] != '0' && buf[0] != '1'))
		return -EINVAL;
	return buf[0] - '0';
}
