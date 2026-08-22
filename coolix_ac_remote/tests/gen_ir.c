#include "coolix_ir_protocol.h"
#include <stdio.h>

static void emit(FILE* f, const char* name, uint32_t code) {
    uint32_t t[COOLIX_IR_MAX_TIMINGS];
    size_t n = 0;
    if(!coolix_ir_encode_code(code, t, &n)) return;
    fprintf(f, "#\nname: %s\ntype: raw\nfrequency: 38000\nduty_cycle: 0.330000\ndata:", name);
    for(size_t i = 0; i < n; i++) fprintf(f, " %u", t[i]);
    fprintf(f, "\n");
}

int main(int argc, char** argv) {
    FILE* f = fopen(argv[1], "w");
    fprintf(f, "Filetype: IR signals file\nVersion: 1\n");

    emit(f, "Power_off", coolix_ir_get_toggle_code(CoolixTogglePowerOff));
    emit(f, "Swing", coolix_ir_get_toggle_code(CoolixToggleSwing));
    emit(f, "Direct", coolix_ir_get_toggle_code(CoolixToggleDirect));
    emit(f, "Turbo", coolix_ir_get_toggle_code(CoolixToggleTurbo));
    emit(f, "Led", coolix_ir_get_toggle_code(CoolixToggleLed));
    emit(f, "Sleep", coolix_ir_get_toggle_code(CoolixToggleSleep));
    for(int e = 0; e < CoolixExtraCount; e++) {
        char nm[32];
        snprintf(nm, sizeof(nm), "%s", coolix_ir_get_extra_name((CoolixExtra)e));
        for(char* p = nm; *p; p++) if(*p == ' ') *p = '_';
        emit(f, nm, coolix_ir_get_extra_code((CoolixExtra)e));
    }

    // Cool mode across the whole setpoint range, fan auto
    for(int temp = COOLIX_TEMP_MIN; temp <= COOLIX_TEMP_MAX; temp++) {
        char nm[32];
        snprintf(nm, sizeof(nm), "Cool_%dC", temp);
        emit(f, nm, coolix_ir_build_state(CoolixModeCool, CoolixFanAuto, temp));
    }
    // Heat mode across the whole setpoint range, fan auto
    for(int temp = COOLIX_TEMP_MIN; temp <= COOLIX_TEMP_MAX; temp++) {
        char nm[32];
        snprintf(nm, sizeof(nm), "Heat_%dC", temp);
        emit(f, nm, coolix_ir_build_state(CoolixModeHeat, CoolixFanAuto, temp));
    }
    // Fan speeds in Cool at 24C
    static const char* fan_nm[] = {"Auto", "Low", "Med", "High"};
    for(int fan = 0; fan < CoolixFanCount; fan++) {
        char nm[32];
        snprintf(nm, sizeof(nm), "Cool_24C_Fan_%s", fan_nm[fan]);
        emit(f, nm, coolix_ir_build_state(CoolixModeCool, (CoolixFan)fan, 24));
    }
    emit(f, "Auto_25C", coolix_ir_build_state(CoolixModeAuto, CoolixFanAuto, 25));
    emit(f, "Dry_24C", coolix_ir_build_state(CoolixModeDry, CoolixFanAuto, 24));
    emit(f, "Fan_only", coolix_ir_build_state(CoolixModeFan, CoolixFanAuto, 24));

    fclose(f);
    (void)argc;
    return 0;
}
