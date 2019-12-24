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

static void mode_handler(int pin, void *arg) {
  struct mgos_fingerprint *finger = (struct mgos_fingerprint *) arg;
  int mode;
  if (!finger) return;

  if (!mgos_fingerprint_svc_mode_get(finger, &mode)) return;
  if (mode == MGOS_FINGERPRINT_MODE_ENROLL) {
    LOG(LL_INFO, ("Entering match mode"));
    mgos_fingerprint_svc_mode_set(finger, MGOS_FINGERPRINT_MODE_MATCH);
    return;
  }
  LOG(LL_INFO, ("Entering enroll mode"));
  mgos_fingerprint_svc_mode_set(finger, MGOS_FINGERPRINT_MODE_ENROLL);
}

static void erase_handler(int pin, void *arg) {
  struct mgos_fingerprint *finger = (struct mgos_fingerprint *) arg;

  uint16_t num_models = 0;
  mgos_fingerprint_model_count(finger, &num_models);

  LOG(LL_INFO, ("Erasing %u model(s) from database", num_models));
  mgos_fingerprint_database_erase(finger);
  mgos_fingerprint_led_aura(finger, MGOS_FINGERPRINT_AURA_FLASHING, 0x40,
                            MGOS_FINGERPRINT_AURA_RED, 3);
}

static void mgos_fingerprint_handler(struct mgos_fingerprint *finger, int ev,
                                     void *ev_data, void *user_data) {
  switch (ev) {
    case MGOS_FINGERPRINT_EV_INITIALIZED:
      break;
    case MGOS_FINGERPRINT_EV_IMAGE:
      mgos_fingerprint_led_aura(finger, MGOS_FINGERPRINT_AURA_FLASHING, 0x08,
                                MGOS_FINGERPRINT_AURA_PURPLE, 2);
      break;
    case MGOS_FINGERPRINT_EV_MATCH_OK:
      mgos_fingerprint_led_aura(finger, MGOS_FINGERPRINT_AURA_BREATHING, 0xF0,
                                MGOS_FINGERPRINT_AURA_BLUE, 1);
      break;
    case MGOS_FINGERPRINT_EV_MATCH_ERROR:
      mgos_fingerprint_led_aura(finger, MGOS_FINGERPRINT_AURA_BREATHING, 0xF0,
                                MGOS_FINGERPRINT_AURA_RED, 1);
      break;
    case MGOS_FINGERPRINT_EV_STATE_MATCH:
      mgos_fingerprint_led_aura(finger, MGOS_FINGERPRINT_AURA_OFF, 0xF0,
                                MGOS_FINGERPRINT_AURA_PURPLE, 0);
      break;
    case MGOS_FINGERPRINT_EV_STATE_ENROLL1:
    case MGOS_FINGERPRINT_EV_STATE_ENROLL2:
      mgos_fingerprint_led_aura(finger, MGOS_FINGERPRINT_AURA_BREATHING, 0x40,
                                MGOS_FINGERPRINT_AURA_BLUE, 0);
      break;
    case MGOS_FINGERPRINT_EV_ENROLL_OK:
      mgos_fingerprint_led_aura(finger, MGOS_FINGERPRINT_AURA_FLASHING, 0x08,
                                MGOS_FINGERPRINT_AURA_BLUE, 5);
      sleep(2);
      break;
    case MGOS_FINGERPRINT_EV_ENROLL_ERROR:
      mgos_fingerprint_led_aura(finger, MGOS_FINGERPRINT_AURA_FLASHING, 0x08,
                                MGOS_FINGERPRINT_AURA_RED, 5);
      sleep(2);
      break;
    default:
      LOG(LL_WARN, ("Unknown event %d", ev));
  }
  (void) ev_data;
  (void) user_data;
}

enum mgos_app_init_result mgos_app_init(void) {
  struct mgos_fingerprint *finger = NULL;
  struct mgos_fingerprint_cfg cfg;

  mgos_fingerprint_config_set_defaults(&cfg);
  cfg.uart_no = 2;
  cfg.handler = mgos_fingerprint_handler;

  if (NULL == (finger = mgos_fingerprint_create(&cfg))) {
    LOG(LL_ERROR, ("Did not find fingerprint sensor"));
    return false;
  }

  // Start service
  mgos_fingerprint_svc_init(finger, 250);

  // Register enroll button
  mgos_gpio_set_mode(mgos_sys_config_get_app_enroll_gpio(),
                     MGOS_GPIO_MODE_INPUT);
  mgos_gpio_set_button_handler(mgos_sys_config_get_app_enroll_gpio(),
                               MGOS_GPIO_PULL_UP, MGOS_GPIO_INT_EDGE_POS, 50,
                               mode_handler, finger);

  // Register erase button
  mgos_gpio_set_mode(mgos_sys_config_get_app_erase_gpio(),
                     MGOS_GPIO_MODE_INPUT);
  mgos_gpio_set_button_handler(mgos_sys_config_get_app_erase_gpio(),
                               MGOS_GPIO_PULL_UP, MGOS_GPIO_INT_EDGE_POS, 50,
                               erase_handler, finger);

  return MGOS_APP_INIT_SUCCESS;
}
