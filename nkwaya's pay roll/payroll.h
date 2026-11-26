#ifndef PAYROLL_H
#define PAYROLL_H
#include <stddef.h>

#define WORKING_DAYS 22
#define MIN_WAGE     30000.0   /* minimum wage we define for the red highlight */
#define DATA_FILE    "payroll.dat"

typedef struct {
    char id[16], name[64], nid[24], dept[16], title[48], type[12];
    char joined[11];            /* YYYY-MM-DD */
    char bank[32];
    char status[12];            /* Active, On Leave, Terminated */
    double basic, transport, medical;
} Employee;

typedef struct { char empId[16], month[8]; int worked, absent; double overtime; } Attendance;

typedef struct {
    char empId[16], month[8];
    double basic, housing, transport, medical, bonus, otPay, gross;
    double tax, ss, absDed, loan, totalDed, net;
    int absent;
} PayrollRec;

typedef struct { char stamp[20], action[24], empId[16], month[8], detail[96]; } HistoryRec;

extern Employee   *g_emps;  extern int g_nEmps;
extern Attendance *g_att;   extern int g_nAtt;
extern PayrollRec *g_pay;   extern int g_nPay;
extern HistoryRec *g_hist;  extern int g_nHist;

extern const char *DEPTS[5];  extern const char *TYPES[3];  extern const char *STATUSES[3];

/* All functions returning const char* return NULL on success or an error text. */
Employee *pr_find(const char *id);
const char *pr_valid_date(const char *d, int allowFuture);
const char *pr_valid_month(const char *m);
const char *pr_add_employee(const Employee *e);
const char *pr_update_employee(const char *id, const char *dept, const char *title,
                               const char *type, double basic, const char *status);
const char *pr_terminate(const char *id);
const char *pr_set_allowances(const char *id, double transport, double medical);
const char *pr_add_attendance(const char *id, const char *month, int worked, int absent, double ot);
const char *pr_calculate(const char *id, const char *month, double bonusPct, double loan, PayrollRec *out);
PayrollRec *pr_find_pay(const char *id, const char *month);
double pr_tax(double gross);
const char *pr_payslip_text(const char *id, const char *month, char *buf, size_t n);
const char *pr_report_text(const char *month, char *buf, size_t n);
int  pr_export_text(const char *filename, const char *text);
void pr_history_add(const char *action, const char *id, const char *month, const char *detail);
int  pr_save(const char *file);
int  pr_load(const char *file);   /* returns 1 if loaded, 0 if no file (not an error) */
void pr_free_all(void);
#endif
