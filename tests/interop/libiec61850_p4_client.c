// SPDX-License-Identifier: GPL-3.0-or-later
// Independent P4 interoperability oracle built against MZ Automation libiec61850.

#include "iec61850_client.h"
#include "hal_thread.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static atomic_int g_report_count;
static atomic_int g_report_int_value;
static atomic_int g_termination_count;
static atomic_int g_termination_error;
static atomic_int g_termination_add_cause;

static void reset_report_state(void)
{
    atomic_store(&g_report_count, 0);
    atomic_store(&g_report_int_value, INT32_MIN);
}

static void report_handler(void* parameter, ClientReport report)
{
    (void) parameter;
    MmsValue* values = ClientReport_getDataSetValues(report);
    if (values != NULL) {
        MmsValue* first = MmsValue_getElement(values, 0);
        if (first != NULL)
            atomic_store(&g_report_int_value, MmsValue_toInt32(first));
    }
    atomic_fetch_add(&g_report_count, 1);
}

static void termination_handler(void* parameter, ControlObjectClient control)
{
    (void) parameter;
    LastApplError error = ControlObjectClient_getLastApplError(control);
    atomic_store(&g_termination_error, (int) error.error);
    atomic_store(&g_termination_add_cause, (int) error.addCause);
    atomic_fetch_add(&g_termination_count, 1);
}

static int wait_for_counter(atomic_int* counter, int minimum, int timeout_ms)
{
    const int step_ms = 20;
    for (int elapsed = 0; elapsed < timeout_ms; elapsed += step_ms) {
        if (atomic_load(counter) >= minimum)
            return 1;
        Thread_sleep(step_ms);
    }
    return atomic_load(counter) >= minimum;
}

static int server_directory_contains(IedConnection connection, const char* expected)
{
    IedClientError error = IED_ERROR_OK;
    LinkedList directory = IedConnection_getServerDirectory(connection, &error, false);
    if (error != IED_ERROR_OK || directory == NULL) {
        fprintf(stderr, "P4_FAIL directory error=%s\n", IedClientError_toString(error));
        return 0;
    }

    int found = 0;
    for (LinkedList entry = LinkedList_getNext(directory); entry != NULL; entry = LinkedList_getNext(entry)) {
        const char* name = (const char*) LinkedList_getData(entry);
        if (name != NULL && strcmp(name, expected) == 0) {
            found = 1;
            break;
        }
    }
    LinkedList_destroy(directory);
    return found;
}

static int read_int32(IedConnection connection, const char* reference, FunctionalConstraint fc, int32_t expected)
{
    IedClientError error = IED_ERROR_OK;
    MmsValue* value = IedConnection_readObject(connection, &error, reference, fc);
    if (error != IED_ERROR_OK || value == NULL) {
        fprintf(stderr, "P4_FAIL read reference=%s error=%s\n", reference, IedClientError_toString(error));
        if (value != NULL)
            MmsValue_delete(value);
        return 0;
    }
    const int32_t observed = MmsValue_toInt32(value);
    MmsValue_delete(value);
    if (observed != expected) {
        fprintf(stderr, "P4_FAIL read reference=%s expected=%d observed=%d\n",
                reference, (int) expected, (int) observed);
        return 0;
    }
    return 1;
}

static int read_bool(IedConnection connection, const char* reference, FunctionalConstraint fc, bool expected)
{
    IedClientError error = IED_ERROR_OK;
    MmsValue* value = IedConnection_readObject(connection, &error, reference, fc);
    if (error != IED_ERROR_OK || value == NULL) {
        fprintf(stderr, "P4_FAIL read reference=%s error=%s\n", reference, IedClientError_toString(error));
        if (value != NULL)
            MmsValue_delete(value);
        return 0;
    }
    const bool observed = MmsValue_getBoolean(value);
    MmsValue_delete(value);
    if (observed != expected) {
        fprintf(stderr, "P4_FAIL read reference=%s expected=%d observed=%d\n",
                reference, expected ? 1 : 0, observed ? 1 : 0);
        return 0;
    }
    return 1;
}

static int exercise_urcb(IedConnection connection)
{
    const char* rcb_reference = "MU01LD0/LLN0.RP.urcb01";
    IedClientError error = IED_ERROR_OK;
    ClientReportControlBlock rcb = IedConnection_getRCBValues(connection, &error, rcb_reference, NULL);
    if (error != IED_ERROR_OK || rcb == NULL) {
        fprintf(stderr, "P4_FAIL urcb_get error=%s\n", IedClientError_toString(error));
        return 0;
    }

    const char* rpt_id = ClientReportControlBlock_getRptId(rcb);
    if (rpt_id == NULL || strcmp(rpt_id, "MU01_LD0_URCB01") != 0) {
        fprintf(stderr, "P4_FAIL urcb_rptid observed=%s\n", rpt_id != NULL ? rpt_id : "<null>");
        ClientReportControlBlock_destroy(rcb);
        return 0;
    }

    reset_report_state();
    IedConnection_installReportHandler(connection, rcb_reference, rpt_id, report_handler, NULL);
    ClientReportControlBlock_setRptEna(rcb, true);
    IedConnection_setRCBValues(connection, &error, rcb, RCB_ELEMENT_RPT_ENA, true);
    if (error != IED_ERROR_OK) {
        fprintf(stderr, "P4_FAIL urcb_enable error=%s\n", IedClientError_toString(error));
        ClientReportControlBlock_destroy(rcb);
        return 0;
    }

    ClientReportControlBlock_setGI(rcb, true);
    IedConnection_setRCBValues(connection, &error, rcb, RCB_ELEMENT_GI, true);
    if (error != IED_ERROR_OK) {
        fprintf(stderr, "P4_FAIL urcb_gi error=%s\n", IedClientError_toString(error));
        ClientReportControlBlock_destroy(rcb);
        return 0;
    }

    const int received = wait_for_counter(&g_report_count, 1, 3500);
    const int report_value = atomic_load(&g_report_int_value);

    ClientReportControlBlock_setRptEna(rcb, false);
    IedConnection_setRCBValues(connection, &error, rcb, RCB_ELEMENT_RPT_ENA, true);
    ClientReportControlBlock_destroy(rcb);

    if (!received || report_value != 42) {
        fprintf(stderr, "P4_FAIL urcb_report count=%d value=%d\n",
                atomic_load(&g_report_count), report_value);
        return 0;
    }
    return 1;
}

static int exercise_brcb_and_external_write(IedConnection connection)
{
    const char* rcb_reference = "MU01LD0/LLN0.BR.brcb01";
    const char* sp_reference = "MU01LD0/XCBR1.SimCfg.setVal";
    IedClientError error = IED_ERROR_OK;
    ClientReportControlBlock rcb = IedConnection_getRCBValues(connection, &error, rcb_reference, NULL);
    if (error != IED_ERROR_OK || rcb == NULL) {
        fprintf(stderr, "P4_FAIL brcb_get error=%s\n", IedClientError_toString(error));
        return 0;
    }

    const char* rpt_id = ClientReportControlBlock_getRptId(rcb);
    if (rpt_id == NULL || strcmp(rpt_id, "MU01_LD0_BRCB01") != 0) {
        fprintf(stderr, "P4_FAIL brcb_rptid observed=%s\n", rpt_id != NULL ? rpt_id : "<null>");
        ClientReportControlBlock_destroy(rcb);
        return 0;
    }

    reset_report_state();
    IedConnection_installReportHandler(connection, rcb_reference, rpt_id, report_handler, NULL);
    ClientReportControlBlock_setRptEna(rcb, true);
    IedConnection_setRCBValues(connection, &error, rcb, RCB_ELEMENT_RPT_ENA, true);
    if (error != IED_ERROR_OK) {
        fprintf(stderr, "P4_FAIL brcb_enable error=%s\n", IedClientError_toString(error));
        ClientReportControlBlock_destroy(rcb);
        return 0;
    }

    IedConnection_writeBooleanValue(connection, &error, sp_reference, IEC61850_FC_SP, true);
    if (error != IED_ERROR_OK) {
        fprintf(stderr, "P4_FAIL external_sp_write_true error=%s\n", IedClientError_toString(error));
        ClientReportControlBlock_destroy(rcb);
        return 0;
    }
    IedConnection_writeBooleanValue(connection, &error, sp_reference, IEC61850_FC_SP, false);
    if (error != IED_ERROR_OK) {
        fprintf(stderr, "P4_FAIL external_sp_write_false error=%s\n", IedClientError_toString(error));
        ClientReportControlBlock_destroy(rcb);
        return 0;
    }

    const int received = wait_for_counter(&g_report_count, 1, 3500);
    const int status_ok = read_bool(connection, sp_reference, IEC61850_FC_SP, false);

    ClientReportControlBlock_setRptEna(rcb, false);
    IedConnection_setRCBValues(connection, &error, rcb, RCB_ELEMENT_RPT_ENA, true);
    ClientReportControlBlock_destroy(rcb);

    if (!received || !status_ok) {
        fprintf(stderr, "P4_FAIL brcb_report count=%d\n", atomic_load(&g_report_count));
        return 0;
    }
    return 1;
}

static int exercise_control(IedConnection connection, const char* reference,
                            ControlModel expected_model, bool enhanced)
{
    ControlObjectClient control = ControlObjectClient_create(reference, connection);
    if (control == NULL) {
        fprintf(stderr, "P4_FAIL control_create reference=%s\n", reference);
        return 0;
    }

    const ControlModel observed_model = ControlObjectClient_getControlModel(control);
    if (observed_model != expected_model) {
        fprintf(stderr, "P4_FAIL control_model reference=%s expected=%d observed=%d\n",
                reference, (int) expected_model, (int) observed_model);
        ControlObjectClient_destroy(control);
        return 0;
    }

    ControlObjectClient_setOrigin(control, "P4-LIBIEC61850", CONTROL_ORCAT_STATION_CONTROL);
    ControlObjectClient_setInterlockCheck(control, false);
    ControlObjectClient_setSynchroCheck(control, false);

    atomic_store(&g_termination_count, 0);
    atomic_store(&g_termination_error, -1);
    atomic_store(&g_termination_add_cause, -1);
    if (enhanced)
        ControlObjectClient_setCommandTerminationHandler(control, termination_handler, NULL);

    MmsValue* ctl_value = MmsValue_newBoolean(true);
    if (ctl_value == NULL) {
        ControlObjectClient_destroy(control);
        return 0;
    }

    int selected = 1;
    if (expected_model == CONTROL_MODEL_SBO_NORMAL)
        selected = ControlObjectClient_select(control) ? 1 : 0;
    else if (expected_model == CONTROL_MODEL_SBO_ENHANCED)
        selected = ControlObjectClient_selectWithValue(control, ctl_value) ? 1 : 0;

    if (!selected) {
        fprintf(stderr, "P4_FAIL control_select reference=%s\n", reference);
        MmsValue_delete(ctl_value);
        ControlObjectClient_destroy(control);
        return 0;
    }

    if (!ControlObjectClient_operate(control, ctl_value, 0)) {
        fprintf(stderr, "P4_FAIL control_operate reference=%s\n", reference);
        MmsValue_delete(ctl_value);
        ControlObjectClient_destroy(control);
        return 0;
    }

    MmsValue_delete(ctl_value);

    if (enhanced) {
        if (!wait_for_counter(&g_termination_count, 1, 3500)) {
            fprintf(stderr, "P4_FAIL command_termination_timeout reference=%s\n", reference);
            ControlObjectClient_destroy(control);
            return 0;
        }
        if (atomic_load(&g_termination_error) != CONTROL_ERROR_NO_ERROR) {
            fprintf(stderr, "P4_FAIL command_termination_negative reference=%s error=%d addCause=%d\n",
                    reference, atomic_load(&g_termination_error), atomic_load(&g_termination_add_cause));
            ControlObjectClient_destroy(control);
            return 0;
        }
    }

    ControlObjectClient_destroy(control);
    return 1;
}

int main(int argc, char** argv)
{
    const char* host = argc > 1 ? argv[1] : "127.0.0.1";
    const int port = argc > 2 ? atoi(argv[2]) : 18102;

    IedConnection connection = IedConnection_create();
    if (connection == NULL) {
        fprintf(stderr, "P4_FAIL connection_create\n");
        return 2;
    }

    IedConnection_setConnectTimeout(connection, 5000);
    IedConnection_setRequestTimeout(connection, 5000);

    IedClientError error = IED_ERROR_OK;
    IedConnection_connect(connection, &error, host, port);
    if (error != IED_ERROR_OK) {
        fprintf(stderr, "P4_FAIL connect host=%s port=%d error=%s\n",
                host, port, IedClientError_toString(error));
        IedConnection_destroy(connection);
        return 3;
    }

    int ok = 1;
    ok = ok && server_directory_contains(connection, "MU01LD0");
    ok = ok && read_int32(connection, "MU01LD0/TCTR1.Amp.instMag.i", IEC61850_FC_MX, 42);
    ok = ok && exercise_urcb(connection);
    ok = ok && exercise_brcb_and_external_write(connection);
    ok = ok && exercise_control(connection, "MU01LD0/GGIO1.SPCSO1", CONTROL_MODEL_DIRECT_NORMAL, false);
    ok = ok && exercise_control(connection, "MU01LD0/GGIO1.SPCSO2", CONTROL_MODEL_SBO_NORMAL, false);
    ok = ok && exercise_control(connection, "MU01LD0/GGIO1.SPCSO3", CONTROL_MODEL_DIRECT_ENHANCED, true);
    ok = ok && exercise_control(connection, "MU01LD0/GGIO1.SPCSO4", CONTROL_MODEL_SBO_ENHANCED, true);

    for (int index = 1; ok && index <= 4; ++index) {
        char reference[96];
        snprintf(reference, sizeof(reference), "MU01LD0/GGIO1.SPCSO%d.stVal", index);
        ok = read_bool(connection, reference, IEC61850_FC_ST, true);
    }

    IedConnection_close(connection);
    IedConnection_destroy(connection);

    if (!ok) {
        fprintf(stderr, "IEDSIM_P4_THIRDPARTY_INTEROP_FAIL stack=libiec61850\n");
        return 1;
    }

    printf("IEDSIM_P4_THIRDPARTY_INTEROP_PASS "
           "stack=libiec61850 pinned=664aa00b447292afdf86330745df1b25328aa98f "
           "association=true discovery=true read42=true urcb_gi=true brcb=true "
           "external_write=true controls=1,2,3,4 termination=2 status_readback=true\n");
    return 0;
}
