#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <sys/time.h>

#include "ethercat.h"

// Outputs
// Offset 0
#define OUTP_WARDROBE 0x01
#define OUTP_WORK_B 0x02
#define OUTP_BEDROOM 0x04
#define OUTP_WORK_A 0x08
#define OUTP_GUEST 0x10
#define OUTP_BATHROOM 0x20

// Inputs
// Offset 0
#define INP_BED 0x08
// Offset 9
#define INP_BEDROOM 0x01
#define INP_GUEST 0x02
#define INP_STAIR_LEFT 0x08
#define INP_BATHROOM 0x20

typedef struct {
  uint8 inp_offs;
  uint8 inp_bit;
  uint8 outp_offs;
  uint8 outp_bit;
  int state;
} multistate_single_output_t;

typedef struct {
  uint8 inp_offs;
  uint8 inp_bit;
  uint8 outp_a_offs;
  uint8 outp_a_bit;
  uint8 outp_b_offs;
  uint8 outp_b_bit;
  int state;
  long int timeout_us;
  struct timeval pressed;
} multistate_dual_output_t;

char IOmap[4096];
int expectedWKC;
volatile int wkc;

volatile int run;

void sighandler(int sig) { run = 0; }

void set_output(uint16 slave_no, uint8 offs, uint8 bit) {
  uint8 *data_ptr;
  data_ptr = ec_slave[slave_no].outputs;
  data_ptr += offs;
  *data_ptr++ |= bit;
}

void clear_output(uint16 slave_no, uint8 offs, uint8 bit) {
  uint8 *data_ptr;
  data_ptr = ec_slave[slave_no].outputs;
  data_ptr += offs;
  *data_ptr++ &= ~bit;
}

int read_output(uint16 slave_no, uint8 offs, uint8 bit) {
  uint8 *data_ptr;
  data_ptr = ec_slave[slave_no].outputs;
  data_ptr += offs;

  return (*data_ptr++ & bit);
}

int read_input(uint16 slave_no, uint8 offs, uint8 bit) {
  uint8 *data_ptr;
  data_ptr = ec_slave[slave_no].inputs;
  data_ptr += offs;

  return (*data_ptr & bit);
}

multistate_dual_output_t init_multistate_dual_output(
    uint8 inp_offs, uint8 inp_bit, uint8 outp_a_offs, uint8 outp_a_bit,
    uint8 outp_b_offs, uint8 outp_b_bit, long int timeout_us) {
  multistate_dual_output_t ret;
  ret.inp_offs = inp_offs;
  ret.inp_bit = inp_bit;
  ret.outp_a_offs = outp_a_offs;
  ret.outp_a_bit = outp_a_bit;
  ret.outp_b_offs = outp_b_offs;
  ret.outp_b_bit = outp_b_bit;
  ret.state = 0;
  ret.pressed.tv_sec = 0;
  ret.pressed.tv_usec = 0;
  ret.timeout_us = timeout_us;

  return ret;
}

multistate_single_output_t init_multistate_single_output(uint8 inp_offs,
                                                         uint8 inp_bit,
                                                         uint8 outp_offs,
                                                         uint8 outp_bit) {
  multistate_single_output_t ret;
  ret.inp_offs = inp_offs;
  ret.inp_bit = inp_bit;
  ret.outp_offs = outp_offs;
  ret.outp_bit = outp_bit;
  ret.state = 0;

  return ret;
}

void update_work_area_switch(multistate_dual_output_t *m) {
  struct timeval now;
  long int dt;

  //Check for timeout
  gettimeofday(&now, NULL);
  dt = (now.tv_sec - m->pressed.tv_sec) * 1000000 + now.tv_usec -
       m->pressed.tv_usec;
  if (dt > m->timeout_us && m->state > 0) m->state = 4;

  //Step through states
  switch (m->state) {
    case 0:
      if (read_input(0, m->inp_offs, m->inp_bit) != 0) {
        gettimeofday(&m->pressed, NULL);
        m->state++;
      }
      break;
    case 1:
      if (read_input(0, m->inp_offs, m->inp_bit) == 0) {
        set_output(0, m->outp_a_offs, m->outp_a_bit);
        set_output(0, m->outp_b_offs, m->outp_b_bit);
        m->state++;
      }
      break;
    case 2:
      if (read_input(0, m->inp_offs, m->inp_bit) != 0) {
        gettimeofday(&m->pressed, NULL);
        m->state++;
      }
      break;
    case 3:
      if (read_input(0, m->inp_offs, m->inp_bit) == 0) {
        set_output(0, m->outp_a_offs, m->outp_a_bit);
        clear_output(0, m->outp_b_offs, m->outp_b_bit);
        m->state++;
      }
      break;
    case 4:
      if (read_input(0, m->inp_offs, m->inp_bit) != 0) {
        gettimeofday(&m->pressed, NULL);
        m->state++;
      }
      break;
    case 5:
      if (read_input(0, m->inp_offs, m->inp_bit) == 0) {
        clear_output(0, m->outp_a_offs, m->outp_a_bit);
        clear_output(0, m->outp_b_offs, m->outp_b_bit);
        m->state = 0;
      }
      break;
  }
}

void update_standard_switch(multistate_single_output_t *m) {
  switch (m->state) {
    case 0:
      if (read_input(0, m->inp_offs, m->inp_bit) != 0) m->state++;
      break;
    case 1:
      if (read_input(0, m->inp_offs, m->inp_bit) == 0) {
        set_output(0, m->outp_offs, m->outp_bit);
        m->state++;
      }
      break;
    case 2:
      if (read_input(0, m->inp_offs, m->inp_bit) != 0) m->state++;
      break;
    case 3:
      if (read_input(0, m->inp_offs, m->inp_bit) == 0) {
        clear_output(0, m->outp_offs, m->outp_bit);
        m->state = 0;
      }
      break;
  }
}

int main(void) {
  int chk;
  run = 1;
  signal(SIGINT, sighandler);
  multistate_single_output_t bathroom, bedroom, guest, wardrobe;
  multistate_dual_output_t work_area;

  bathroom = init_multistate_single_output(9, INP_BATHROOM, 0, OUTP_BATHROOM);
  bedroom = init_multistate_single_output(9, INP_BEDROOM, 0, OUTP_BEDROOM);
  guest = init_multistate_single_output(9, INP_GUEST, 0, OUTP_GUEST);
  wardrobe = init_multistate_single_output(0, INP_BED, 0, OUTP_WARDROBE);
  work_area = init_multistate_dual_output(9, INP_STAIR_LEFT, 0, OUTP_WORK_A, 0,
                                          OUTP_WORK_B, 800000);

  if (ec_init("eth0")) {
    printf("Init succeded\n");
    if (ec_config_init(FALSE) > 0) {
      printf("%d slaves found and configured\n", ec_slavecount);

      ec_config_map(&IOmap);
      ec_configdc();

      printf("Slaves mapped\n");

      ec_statecheck(0, EC_STATE_SAFE_OP, EC_TIMEOUTSTATE * 4);

      expectedWKC = (ec_group[0].outputsWKC * 2) + ec_group[0].inputsWKC;

      ec_slave[0].state = EC_STATE_OPERATIONAL;

      ec_send_processdata();
      ec_receive_processdata(EC_TIMEOUTRET);
      ec_writestate(0);

      chk = 200;
      do {
        ec_send_processdata();
        ec_receive_processdata(EC_TIMEOUTRET);
        ec_statecheck(0, EC_STATE_OPERATIONAL, 50000);
      } while (chk-- && (ec_slave[0].state != EC_STATE_OPERATIONAL));

      printf("Entering loop\n");

      while (run) {
        ec_send_processdata();
        wkc = ec_receive_processdata(EC_TIMEOUTRET);

        if (wkc >= expectedWKC) {
          uint8 tmp;
          tmp = *(ec_slave[0].inputs + 0);
          if (tmp != 0) printf("%2.2x\n", tmp);

          update_standard_switch(&bathroom);
          update_standard_switch(&bedroom);
          update_standard_switch(&guest);
          update_standard_switch(&wardrobe);
          update_work_area_switch(&work_area);
        }

        osal_usleep(5000);
      }
    }
    printf("SIGINT\n");

    ec_close();
  } else {
    printf("Init failed\n");
  }

  return 0;
}

