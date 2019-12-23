/*
 * Copyright 2019 Pim van Pelt <pim@ipng.nl>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mgos.h"
#include "mgos_config.h"
#include "mgos_fingerprint.h"

#define MODE_NONE 0
#define MODE_MATCH 1
#define MODE_ENROLL 2
#define STATE_NONE 0
#define STATE_FIRST_IMAGE 1
#define STATE_SECOND_IMAGE 2

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
  switch (p) {
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

static void enroll_cb(struct mgos_fingerprint *finger) {
  int16_t p;

  switch (s_state) {
    case STATE_FIRST_IMAGE:
      if (MGOS_FINGERPRINT_OK !=
          (p = mgos_fingerprint_image_genchar(finger, 1))) {
        LOG(LL_ERROR, ("Could not generate fingerprint in slot 1"));
        goto err;
      }
      LOG(LL_INFO, ("Stored fingerprint in slot 1: Remove finger"));

      while (p != MGOS_FINGERPRINT_NOFINGER) {
        p = mgos_fingerprint_image_get(finger);
        usleep(50000);
      }

      s_state = STATE_SECOND_IMAGE;
      mgos_fingerprint_led_aura(finger, MGOS_FINGERPRINT_AURA_BREATHING, 0x40,
                                MGOS_FINGERPRINT_AURA_BLUE, 0);
      return;

      break;
    case STATE_SECOND_IMAGE: {
      int16_t fid;

      if (MGOS_FINGERPRINT_OK != mgos_fingerprint_image_genchar(finger, 2)) {
        LOG(LL_ERROR, ("Could not generate fingerprint in slot 2"));
        goto err;
      }
      LOG(LL_INFO, ("Stored fingerprint in slot 2"));

      if (MGOS_FINGERPRINT_OK != mgos_fingerprint_model_combine(finger)) {
        LOG(LL_ERROR, ("Could not combine fingerprints into a model"));
        goto err;
      }
      LOG(LL_INFO, ("Fingerprints combined successfully"));

      if (MGOS_FINGERPRINT_OK != mgos_fingerprint_get_free_id(finger, &fid)) {
        LOG(LL_ERROR, ("Could not get free flash slot"));
        goto err;
      }

      if (MGOS_FINGERPRINT_OK != mgos_fingerprint_model_store(finger, fid, 1)) {
        LOG(LL_ERROR, ("Could not store model in flash slot %u", fid));
        goto err;
      }
      LOG(LL_INFO, ("Model stored in flash slot %d", fid));
      mgos_fingerprint_led_aura(finger, MGOS_FINGERPRINT_AURA_FLASHING, 0x08,
                                MGOS_FINGERPRINT_AURA_BLUE, 5);
      sleep(2);

      s_mode = MODE_ENROLL;
      s_state = STATE_FIRST_IMAGE;
      mgos_fingerprint_led_aura(finger, MGOS_FINGERPRINT_AURA_BREATHING, 0x40,
                                MGOS_FINGERPRINT_AURA_BLUE, 0);
      return;
    }
  }

  // Bail with error, and return to match mode.
err:
  LOG(LL_ERROR, ("Error, returning to enroll mode"));
  s_state = STATE_FIRST_IMAGE;
  s_mode = MODE_ENROLL;
  mgos_fingerprint_led_aura(finger, MGOS_FINGERPRINT_AURA_FLASHING, 0x08,
                            MGOS_FINGERPRINT_AURA_RED, 5);
  sleep(2);
  mgos_fingerprint_led_aura(finger, MGOS_FINGERPRINT_AURA_BREATHING, 0x40,
                            MGOS_FINGERPRINT_AURA_BLUE, 0);
}

static void timer_cb(void *arg) {
  struct mgos_fingerprint *finger = (struct mgos_fingerprint *) arg;

  int16_t p = mgos_fingerprint_image_get(finger);
  if (p == MGOS_FINGERPRINT_NOFINGER) {
    return;
  } else if (p != MGOS_FINGERPRINT_OK) {
    LOG(LL_ERROR, ("image_get() error: %d", p));
    return;
  }

  LOG(LL_INFO, ("Fingerprint image taken"));
  mgos_fingerprint_led_aura(finger, MGOS_FINGERPRINT_AURA_FLASHING, 0x08,
                            MGOS_FINGERPRINT_AURA_PURPLE, 2);

  switch (s_mode) {
    case MODE_MATCH:
      return match_cb(finger);
      break;
    case MODE_ENROLL:
      return enroll_cb(finger);
      break;
  }
}

static void enroll_handler(int pin, void *arg) {
  struct mgos_fingerprint *finger = (struct mgos_fingerprint *) arg;

  if (s_mode == MODE_ENROLL) {
    LOG(LL_INFO, ("Exiting enroll mode"));
    s_state = STATE_NONE;
    s_mode = MODE_MATCH;
    mgos_fingerprint_led_aura(finger, MGOS_FINGERPRINT_AURA_OFF, 0xF0,
                              MGOS_FINGERPRINT_AURA_PURPLE, 0);
    return;
  }
  LOG(LL_INFO, ("Entering enroll mode"));
  s_mode = MODE_ENROLL;
  s_state = STATE_FIRST_IMAGE;
  mgos_fingerprint_led_aura(finger, MGOS_FINGERPRINT_AURA_BREATHING, 0x40,
                            MGOS_FINGERPRINT_AURA_BLUE, 0);
}

static void erase_handler(int pin, void *arg) {
  struct mgos_fingerprint *finger = (struct mgos_fingerprint *) arg;

  uint16_t num_models = 0;
  mgos_fingerprint_model_count(finger, &num_models);

  LOG(LL_INFO, ("Erasing %u model(s) from database", num_models));
  s_mode = MODE_MATCH;
  s_state = STATE_NONE;
  mgos_fingerprint_database_erase(finger);
  mgos_fingerprint_led_aura(finger, MGOS_FINGERPRINT_AURA_FLASHING, 0x40,
                            MGOS_FINGERPRINT_AURA_RED, 3);
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

  // Register enroll button
  mgos_gpio_set_mode(mgos_sys_config_get_app_enroll_gpio(),
                     MGOS_GPIO_MODE_INPUT);
  mgos_gpio_set_button_handler(mgos_sys_config_get_app_enroll_gpio(),
                               MGOS_GPIO_PULL_UP, MGOS_GPIO_INT_EDGE_POS, 50,
                               enroll_handler, finger);

  // Register erase button
  mgos_gpio_set_mode(mgos_sys_config_get_app_erase_gpio(),
                     MGOS_GPIO_MODE_INPUT);
  mgos_gpio_set_button_handler(mgos_sys_config_get_app_erase_gpio(),
                               MGOS_GPIO_PULL_UP, MGOS_GPIO_INT_EDGE_POS, 50,
                               erase_handler, finger);

  return MGOS_APP_INIT_SUCCESS;
}
