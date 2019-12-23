#include "mgos.h"
#include "mgos_fingerprint.h"

#define MODE_NONE 0
#define MODE_MATCH 1
#define MODE_LEARN 2
#define STATE_NONE 0

static uint8_t s_mode = MODE_MATCH;
static uint8_t s_state = STATE_NONE;

static void match_cb(struct mgos_fingerprint *finger) {
  int16_t p;
  if (!finger) return;

  p = mgos_fingerprint_image_genchar(finger, 1);
  if (p != MGOS_FINGERPRINT_OK) {
    LOG(LL_ERROR, ("Error image_genchar(): %d!", p));
    goto out;
  }

  uint16_t fid = -1, score = 0;
  p = mgos_fingerprint_database_search(finger, &fid, &score, 1);
  switch(p) {
  case MGOS_FINGERPRINT_OK:
    LOG(LL_INFO, ("Fingerprint match: fid=%d score=%d", fid, score));
    break;
  case MGOS_FINGERPRINT_NOTFOUND:
    LOG(LL_INFO, ("Fingerprint not found"));
    break;
  default:
    LOG(LL_ERROR, ("Error database_search(): %d!", p));
  }

out:
  mgos_fingerprint_led_aura(finger, MGOS_FINGERPRINT_AURA_BREATHING, 0xA0,
                                p == MGOS_FINGERPRINT_OK
                                    ? MGOS_FINGERPRINT_AURA_BLUE
                                    : MGOS_FINGERPRINT_AURA_RED,
                                1);
  return;
}

static void learn_cb(struct mgos_fingerprint *finger) {
  return;
}

static void timer_cb(void *arg) {
  struct mgos_fingerprint *finger = (struct mgos_fingerprint *)arg;

  int16_t p = mgos_fingerprint_image_get(finger);
  if (p == MGOS_FINGERPRINT_NOFINGER) {
    return;
  } else if (p != MGOS_FINGERPRINT_OK) {
    LOG(LL_ERROR, ("image_get() error: %d", p));
    return;
  }

  LOG(LL_INFO, ("Fingerprint image taken"));
  mgos_fingerprint_led_aura(finger, MGOS_FINGERPRINT_AURA_FLASHING, 0x08, MGOS_FINGERPRINT_AURA_PURPLE, 2);

  switch(s_mode) {
  case MODE_MATCH:
    return match_cb(finger);
    break;
  case MODE_LEARN:
    return learn_cb(finger);
    break;
  }
}


enum mgos_app_init_result mgos_app_init(void) {
  struct mgos_fingerprint *finger = NULL;
  struct mgos_fingerprint_cfg cfg;

  mgos_fingerprint_config_set_defaults(&cfg);
  cfg.uart_no = 2;

  if (NULL == (finger = mgos_fingerprint_create(&cfg))) {
    LOG(LL_ERROR, ("Did not find fingerprint sensor"));
    return false;
  }

  mgos_set_timer(250, true, timer_cb, finger);

  return MGOS_APP_INIT_SUCCESS;
}
