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
#include "mgos_gpio.h"
#include "mgos_timers.h"

struct mgos_fingerprint *finger = NULL;
struct mgos_fingerprint_cfg cfg;


static mgos_timer_id s_door_timer = MGOS_INVALID_TIMER_ID; // The var for door hold duration
static mgos_timer_id s_hold_timer = MGOS_INVALID_TIMER_ID; // The var for long press detection
static mgos_timer_id s_button_timer = MGOS_INVALID_TIMER_ID; // The var for long press detection

static double startTime = 0; //Variable timer for measuring button press

static void mode_handler(void *arg);
static void erase_handler(void *arg);

static void clear_led(void *arg) {
  mgos_gpio_write(mgos_sys_config_get_app_Green_LED(), 0);
  mgos_gpio_write(mgos_sys_config_get_app_Red_LED(), 0);
  (void) arg;
}

static void GreenLED(bool level) {
  mgos_gpio_write(mgos_sys_config_get_app_Red_LED(), false);
  mgos_gpio_write(mgos_sys_config_get_app_Green_LED(), level);

}

static void RedLED(bool level) {
  mgos_gpio_write(mgos_sys_config_get_app_Green_LED(), false);
  mgos_gpio_write(mgos_sys_config_get_app_Red_LED(), level);
  
}

static void OrangeLED(bool level) {
  mgos_gpio_write(mgos_sys_config_get_app_Green_LED(), level);
  mgos_gpio_write(mgos_sys_config_get_app_Red_LED(), level);

}


static void clear_door(void *arg) {
  s_door_timer = MGOS_INVALID_TIMER_ID;
  mgos_gpio_write(mgos_sys_config_get_app_door(), false);
  (void) arg;
}

static void DoorAction() {
  mgos_gpio_write(mgos_sys_config_get_app_door(), true);
  s_door_timer = mgos_set_timer(500, 0, clear_door, NULL);
}



static void button_timer_cb(void *arg)
{
  (void)arg;
  double duration = mgos_uptime() - startTime;
  // LOG(LL_INFO, ("Button Timer Callback after %.2lf seconds", duration));

  if (duration > 4 && duration < 8) {
    OrangeLED(true);
    s_button_timer = mgos_set_timer(mgos_sys_config_get_app_timeout()*1000, 0, clear_led, NULL);
    LOG(LL_INFO, ("Need to enter mode handler"));
    mode_handler(finger);
  }

  if (duration >8) {
    RedLED(true);
    // s_button_timer = mgos_set_timer(mgos_sys_config_get_app_timeout(), 0, clear_led, NULL);
    LOG(LL_INFO, ("Need to enter clear handler"));
    erase_handler(finger);
  }

}

static void button_cb(int pin, void *arg)
{
  (void)arg;
  int duration = 5000;

  if (mgos_gpio_read(mgos_sys_config_get_app_button()) == false) {
    LOG(LL_INFO, ("Button Pressed"));
    GreenLED(true);
    startTime = mgos_uptime();
    s_hold_timer = mgos_set_timer(duration, MGOS_TIMER_REPEAT, button_timer_cb, arg);

  } else { //Button released
      double duration = mgos_uptime() - startTime;
      LOG(LL_INFO, ("Button Released after %.2lf seconds", duration));
      if (s_hold_timer != MGOS_INVALID_TIMER_ID) {
        // LOG(LL_INFO, ("Clearing button timer"));
        mgos_clear_timer(s_hold_timer);
      }
      if (duration < 2.0) {
          mgos_fingerprint_svc_mode_set(finger, MGOS_FINGERPRINT_MODE_MATCH);
          LOG(LL_INFO, ("Open Door"));
          DoorAction();
          GreenLED(false);

      }
  }
}

static void mode_handler(void *arg) {
  // struct mgos_fingerprint *finger = (struct mgos_fingerprint *) arg;
  int mode;
  if (!finger) return;

  if (!mgos_fingerprint_svc_mode_get(finger, &mode)) return;
  if (mode == MGOS_FINGERPRINT_MODE_ENROLL) {
    LOG(LL_INFO, ("Entering match mode"));
    mgos_fingerprint_svc_mode_set(finger, MGOS_FINGERPRINT_MODE_MATCH);
    mgos_set_timer(500, 0, clear_led, NULL);
    return;
  }
  LOG(LL_INFO, ("Entering enroll mode"));
  mgos_fingerprint_svc_mode_set(finger, MGOS_FINGERPRINT_MODE_ENROLL);
}

static void erase_handler(void *arg) {
  LOG(LL_INFO, ("Calling Erase Handler"));
  // struct mgos_fingerprint *finger = (struct mgos_fingerprint *) arg;

  uint16_t num_models = 0;
  mgos_fingerprint_model_count(finger, &num_models);

  LOG(LL_INFO, ("Erasing %u model(s) from database", num_models));
  mgos_fingerprint_database_erase(finger);
  mgos_fingerprint_led_aura(finger, MGOS_FINGERPRINT_AURA_FLASHING, 0x40,
                            MGOS_FINGERPRINT_AURA_RED, 3);
  mgos_set_timer(2000, 0, clear_led, NULL);
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
    case MGOS_FINGERPRINT_EV_MATCH_OK: {
      uint32_t pack = *((uint32_t *) ev_data);
      uint16_t finger_id = pack & 0xFFFF;
      uint16_t score = pack >> 16;

      LOG(LL_INFO, ("Matched finger ID %u with score %u", finger_id, score));
      mgos_fingerprint_led_aura(finger, MGOS_FINGERPRINT_AURA_BREATHING, 0xF0,
                                MGOS_FINGERPRINT_AURA_BLUE, 1);
      LOG(LL_INFO, ("Open Door"));
      DoorAction();
      GreenLED(true);
      mgos_set_timer(2000, false, clear_led, NULL);
      break;
    }
    case MGOS_FINGERPRINT_EV_MATCH_ERROR:
      mgos_fingerprint_led_aura(finger, MGOS_FINGERPRINT_AURA_BREATHING, 0xF0,
                                MGOS_FINGERPRINT_AURA_RED, 1);
      LOG(LL_INFO, ("Fingerprint did not match"));
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
    case MGOS_FINGERPRINT_EV_ENROLL_OK: {
      uint32_t finger_id = *((uint32_t *) ev_data);
      LOG(LL_INFO, ("Stored model at finger ID %u", finger_id));
      mgos_fingerprint_led_aura(finger, MGOS_FINGERPRINT_AURA_FLASHING, 0x08,
                                MGOS_FINGERPRINT_AURA_BLUE, 5);
      sleep(2);
      mgos_fingerprint_svc_mode_set(finger, MGOS_FINGERPRINT_MODE_ENROLL);
      mgos_set_timer(100, false, mode_handler, finger);
      break;
    }
    case MGOS_FINGERPRINT_EV_ENROLL_ERROR:
      mgos_fingerprint_led_aura(finger, MGOS_FINGERPRINT_AURA_FLASHING, 0x08,
                                MGOS_FINGERPRINT_AURA_RED, 5);
      LOG(LL_INFO, ("Fingerprint model not stored"));
      sleep(2);
      mgos_fingerprint_svc_mode_set(finger, MGOS_FINGERPRINT_MODE_ENROLL);
      mgos_set_timer(1000, false, mode_handler, finger);

      break;
    default:
      LOG(LL_WARN, ("Unknown event %d", ev));
  }
  (void) ev_data;
  (void) user_data;
}

enum mgos_app_init_result mgos_app_init(void) {
  

  mgos_fingerprint_config_set_defaults(&cfg);
  cfg.uart_no = 2;
  cfg.handler = mgos_fingerprint_handler;
  cfg.enroll_timeout_secs = mgos_sys_config_get_app_timeout();

  if (NULL == (finger = mgos_fingerprint_create(&cfg))) {
    LOG(LL_ERROR, ("Did not find fingerprint sensor"));
    return false;
  }

  mgos_gpio_set_mode(mgos_sys_config_get_app_Green_LED(), 1);
  mgos_gpio_set_mode(mgos_sys_config_get_app_Red_LED(), 1);
  mgos_gpio_set_mode(mgos_sys_config_get_app_door(), 1);
  


  // Start service
  mgos_fingerprint_svc_init(finger, 250);

  // Register  button

  mgos_gpio_set_mode(mgos_sys_config_get_app_button(), MGOS_GPIO_MODE_INPUT);
  mgos_gpio_set_button_handler(mgos_sys_config_get_app_button(), MGOS_GPIO_PULL_UP, MGOS_GPIO_INT_EDGE_ANY, 50, button_cb, finger);

  // mgos_gpio_set_mode(mgos_sys_config_get_app_button(),
  //                    MGOS_GPIO_MODE_INPUT);
  // mgos_gpio_set_button_handler(mgos_sys_config_get_app_button(),
  //                              MGOS_GPIO_PULL_UP, MGOS_GPIO_INT_EDGE_POS, 50,
  //                              mode_handler, finger);

  // // Register erase button
  // mgos_gpio_set_mode(mgos_sys_config_get_app_erase_gpio(),
  //                    MGOS_GPIO_MODE_INPUT);
  // mgos_gpio_set_button_handler(mgos_sys_config_get_app_erase_gpio(),
  //                              MGOS_GPIO_PULL_UP, MGOS_GPIO_INT_EDGE_POS, 50,
  //                              erase_handler, finger);

  return MGOS_APP_INIT_SUCCESS;
}
