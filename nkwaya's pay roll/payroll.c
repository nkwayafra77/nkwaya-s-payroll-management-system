#include "payroll.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

Employee *g_emps; int g_nEmps;  Attendance *g_att; int g_nAtt;
PayrollRec *g_pay; int g_nPay;  HistoryRec *g_hist; int g_nHist;
const char *DEPTS[5]    = {"Finance","IT","Sales","Operations","HR"};
const char *TYPES[3]    = {"Permanent","Contract","Intern"};
const char *STATUSES[3] = {"Active","On Leave","Terminated"};

static char g_err[128];
static const char *fail(const char *msg) { snprintf(g_err, sizeof g_err, "%s", msg); return g_err; }
static int in_list(const char *v, const char **l, int n) { for (int i=0;i<n;i++) if (!strcmp(v,l[i])) return 1; return 0; }
static void trim(char *s) {
    size_t n = strlen(s), a = 0;
    while (s[a]==' '||s[a]=='\t') a++;
    memmove(s, s+a, n-a+1); n = strlen(s);
    while (n && (s[n-1]==' '||s[n-1]=='\t'||s[n-1]=='\n'||s[n-1]=='\r')) s[--n] = 0;
}
static void copy(char *dst, size_t n, const char *src) { snprintf(dst, n, "%s", src ? src : ""); trim(dst); }

/* ---------- helpers ---------- */
Employee *pr_find(const char *id) {
    for (int i=0;i<g_nEmps;i++) if (!strcmp(g_emps[i].id, id)) return &g_emps[i];
    return NULL;
}
PayrollRec *pr_find_pay(const char *id, const char *month) {
    for (int i=0;i<g_nPay;i++) if (!strcmp(g_pay[i].empId,id) && !strcmp(g_pay[i].month,month)) return &g_pay[i];
    return NULL;
}
static Attendance *find_att(const char *id, const char *month) {
    for (int i=0;i<g_nAtt;i++) if (!strcmp(g_att[i].empId,id) && !strcmp(g_att[i].month,month)) return &g_att[i];
    return NULL;
}
const char *pr_valid_date(const char *d, int allowFuture) {
    int y,m,dd; char extra;
    if (!d || sscanf(d, "%4d-%2d-%2d%c", &y,&m,&dd,&extra)!=3 || strlen(d)!=10 || d[4]!='-' || d[7]!='-')
        return fail("Date must be in format YYYY-MM-DD");
    static const int dm[]={31,28,31,30,31,30,31,31,30,31,30,31};
    int leap = (y%4==0 && y%100!=0) || y%400==0;
    if (y<1900 || m<1 || m>12 || dd<1 || dd > dm[m-1] + (m==2&&leap)) return fail("Date does not exist");
    if (!allowFuture) {
        time_t t=time(NULL); struct tm *tm=localtime(&t);
        int today = (tm->tm_year+1900)*10000 + (tm->tm_mon+1)*100 + tm->tm_mday;
        if (y*10000+m*100+dd > today) return fail("Date of joining cannot be in the future");
    }
    return NULL;
}
const char *pr_valid_month(const char *s) {
    int m,y; char extra;
    if (!s || strlen(s)!=7 || s[2]!='-' || sscanf(s,"%2d-%4d%c",&m,&y,&extra)!=2) return fail("Month must be in format MM-YYYY");
    if (m<1||m>12||y<1900||y>2100) return fail("Invalid month or year");
    return NULL;
}
void pr_history_add(const char *action, const char *id, const char *month, const char *detail) {
    g_hist = realloc(g_hist, (g_nHist+1)*sizeof(HistoryRec));
    HistoryRec *h = &g_hist[g_nHist++]; memset(h,0,sizeof *h);
    time_t t=time(NULL); strftime(h->stamp,sizeof h->stamp,"%Y-%m-%d %H:%M:%S",localtime(&t));
    copy(h->action,sizeof h->action,action); copy(h->empId,sizeof h->empId,id);
    copy(h->month,sizeof h->month,month);    copy(h->detail,sizeof h->detail,detail);
}

/* ---------- Req 1: register ---------- */
const char *pr_add_employee(const Employee *in) {
    Employee e = *in; const char *r;
    trim(e.id); trim(e.name); trim(e.nid); trim(e.title); trim(e.bank);
    if (!*e.id)   return fail("Employee ID cannot be empty");
    if (!*e.name) return fail("Name cannot be empty");
    if (!*e.nid)  return fail("National ID cannot be empty");
    if (pr_find(e.id)) return fail("Employee ID already exists");
    for (int i=0;i<g_nEmps;i++) if (!strcmp(g_emps[i].nid,e.nid)) return fail("National ID already exists");
    if (!in_list(e.dept,DEPTS,5))  return fail("Invalid department");
    if (!in_list(e.type,TYPES,3))  return fail("Invalid employment type");
    if (!*e.title) return fail("Job title cannot be empty");
    if ((r = pr_valid_date(e.joined,0))) return r;
    if (!(e.basic > 0)) return fail("Basic salary must be greater than 0");
    if (e.transport<0) e.transport=0;
    if (e.medical<0)   e.medical=0;
    copy(e.status,sizeof e.status,"Active");
    g_emps = realloc(g_emps,(g_nEmps+1)*sizeof(Employee)); g_emps[g_nEmps++] = e;
    pr_history_add("REGISTER", e.id, "", e.name);
    return NULL;
}

/* ---------- Req 8: update / remove ---------- */
const char *pr_update_employee(const char *id, const char *dept, const char *title,
                               const char *type, double basic, const char *status) {
    Employee *e = pr_find(id); char t[48];
    if (!e) return fail("Employee not found");
    copy(t,sizeof t,title);
    if (!in_list(dept,DEPTS,5))     return fail("Invalid department");
    if (!in_list(type,TYPES,3))     return fail("Invalid employment type");
    if (!in_list(status,STATUSES,3))return fail("Invalid status");
    if (!*t)         return fail("Job title cannot be empty");
    if (!(basic > 0))return fail("Basic salary must be greater than 0");
    if (basic != e->basic) {
        char d[96]; snprintf(d,sizeof d,"Salary %.2f -> %.2f",e->basic,basic);
        pr_history_add("SALARY_CHANGE", id, "", d);
    }
    copy(e->dept,sizeof e->dept,dept); copy(e->title,sizeof e->title,t);
    copy(e->type,sizeof e->type,type); copy(e->status,sizeof e->status,status);
    e->basic = basic;
    pr_history_add("UPDATE", id, "", e->name);
    return NULL;
}
const char *pr_terminate(const char *id) {
    Employee *e = pr_find(id);
    if (!e) return fail("Employee not found");
    if (!strcmp(e->status,"Terminated")) return fail("Employee is already terminated");
    copy(e->status,sizeof e->status,"Terminated");
    pr_history_add("TERMINATE", id, "", e->name);
    return NULL;
}

/* ---------- Req 5: allowances ---------- */
const char *pr_set_allowances(const char *id, double transport, double medical) {
    Employee *e = pr_find(id);
    if (!e) return fail("Employee not found");
    if (transport<0 || medical<0) return fail("Allowance amounts must be >= 0");
    e->transport = transport; e->medical = medical;
    pr_history_add("ALLOWANCE", id, "", "Transport/medical updated");
    return NULL;
}

/* ---------- Req 3: attendance ---------- */
const char *pr_add_attendance(const char *id, const char *month, int worked, int absent, double ot) {
    const char *r; Employee *e = pr_find(id);
    if (!e) return fail("Employee not found");
    if (!strcmp(e->status,"Terminated")) return fail("Employee is terminated");
    if ((r = pr_valid_month(month))) return r;
    if (worked<0 || absent<0) return fail("Days cannot be negative");
    if (worked+absent > WORKING_DAYS) return fail("Days worked + days absent cannot exceed 22");
    if (ot<0 || ot>60) return fail("Overtime hours must be between 0 and 60");
    if (pr_find_pay(id,month)) return fail("Payroll already calculated for this month");
    Attendance *a = find_att(id,month);
    if (!a) { g_att = realloc(g_att,(g_nAtt+1)*sizeof(Attendance)); a = &g_att[g_nAtt++]; memset(a,0,sizeof *a); }
    copy(a->empId,sizeof a->empId,id); copy(a->month,sizeof a->month,month);
    a->worked=worked; a->absent=absent; a->overtime=ot;
    char d[96]; snprintf(d,sizeof d,"worked %d absent %d OT %.1f",worked,absent,ot);
    pr_history_add("ATTENDANCE", id, month, d);
    return NULL;
}

/* ---------- Req 4,5,6: salary ---------- */
double pr_tax(double g) {
    if (g <= 60000) return 0;
    if (g <= 100000) return (g-60000)*0.20;
    return 8000 + (g-100000)*0.30;
}
const char *pr_calculate(const char *id, const char *month, double bonusPct, double loan, PayrollRec *out) {
    const char *r; Employee *e = pr_find(id);
    if (!e) return fail("Employee not found");
    if (!strcmp(e->status,"Terminated")) return fail("Terminated employees are excluded from payroll");
    if ((r = pr_valid_month(month))) return r;
    if (bonusPct<0 || bonusPct>20) return fail("Bonus percentage must be between 0 and 20");
    if (loan<0) return fail("Loan repayment must be >= 0");
    if (pr_find_pay(id,month)) return fail("This employee was already paid for this month");
    Attendance *a = find_att(id,month);
    if (!a) return fail("Record attendance for this month first");

    PayrollRec p; memset(&p,0,sizeof p);
    copy(p.empId,sizeof p.empId,id); copy(p.month,sizeof p.month,month);
    p.basic = e->basic; p.housing = 0.15*e->basic; p.transport = e->transport; p.medical = e->medical;
    p.bonus = e->basic*bonusPct/100.0;
    p.otPay = a->overtime * (e->basic/176.0) * 1.5;
    p.gross = p.basic+p.housing+p.transport+p.medical+p.bonus+p.otPay;
    p.tax = pr_tax(p.gross);
    p.ss  = 0.03*e->basic;
    p.absent = a->absent;
    p.absDed = a->absent * (e->basic/WORKING_DAYS);
    p.loan = loan;
    p.totalDed = p.tax+p.ss+p.absDed+p.loan;
    if (p.totalDed > p.gross) return fail("Total deductions exceed gross salary - payroll not saved");
    p.net = p.gross - p.totalDed;
    g_pay = realloc(g_pay,(g_nPay+1)*sizeof(PayrollRec)); g_pay[g_nPay++] = p;
    char d[96]; snprintf(d,sizeof d,"gross %.2f net %.2f",p.gross,p.net);
    pr_history_add("SALARY_CALC", id, month, d);
    if (out) *out = p;
    return NULL;
}

/* ---------- Req 7: payslip ---------- */
#define APPEND(...) do { if (off < n) off += snprintf(buf+off, n-off, __VA_ARGS__); } while(0)
const char *pr_payslip_text(const char *id, const char *month, char *buf, size_t n) {
    Employee *e = pr_find(id); size_t off = 0;
    if (!e) return fail("Employee not found");
    PayrollRec *p = pr_find_pay(id,month);
    if (!p) return fail("No payroll record for this employee and month");
    APPEND("================ PAYSLIP ================\n");
    APPEND("Month        : %s\nEmployee ID  : %s\nName         : %s\n", p->month,e->id,e->name);
    APPEND("Department   : %s\nJob title    : %s\nType         : %s\nBank account : %s\n", e->dept,e->title,e->type,e->bank);
    APPEND("-----------------------------------------\n");
    APPEND("Basic salary         : %12.2f\nHousing allowance    : %12.2f\nTransport allowance  : %12.2f\n", p->basic,p->housing,p->transport);
    APPEND("Medical allowance    : %12.2f\nPerformance bonus    : %12.2f\nOvertime pay         : %12.2f\n", p->medical,p->bonus,p->otPay);
    APPEND("GROSS SALARY         : %12.2f\n-----------------------------------------\n", p->gross);
    APPEND("Income tax           : %12.2f\nSocial security      : %12.2f\nAbsence deduction    : %12.2f\n", p->tax,p->ss,p->absDed);
    APPEND("Loan repayment       : %12.2f\nTOTAL DEDUCTIONS     : %12.2f\n-----------------------------------------\n", p->loan,p->totalDed);
    APPEND("NET SALARY           : %12.2f\n=========================================\n", p->net);
    pr_history_add("PAYSLIP", id, month, "Payslip generated");
    return NULL;
}

/* ---------- Req 10: report ---------- */
const char *pr_report_text(const char *month, char *buf, size_t n) {
    const char *r; size_t off = 0;
    if ((r = pr_valid_month(month))) return r;
    int active=0, perDept[5]={0}; double deptCost[5]={0};
    for (int i=0;i<g_nEmps;i++) if (strcmp(g_emps[i].status,"Terminated")) {
        active++; for (int d=0;d<5;d++) if (!strcmp(g_emps[i].dept,DEPTS[d])) perDept[d]++;
    }
    double gross=0,ded=0,net=0,tax=0,ss=0; int cnt=0; PayrollRec *top=NULL;
    for (int i=0;i<g_nPay;i++) if (!strcmp(g_pay[i].month,month)) {
        PayrollRec *p=&g_pay[i]; cnt++;
        gross+=p->gross; ded+=p->totalDed; net+=p->net; tax+=p->tax; ss+=p->ss;
        if (!top || p->net > top->net) top = p;
        Employee *e = pr_find(p->empId);
        if (e) for (int d=0;d<5;d++) if (!strcmp(e->dept,DEPTS[d])) deptCost[d]+=p->gross;
    }
    APPEND("========= PAYROLL REPORT %s =========\n", month);
    APPEND("Total active employees: %d\n", active);
    for (int d=0;d<5;d++) APPEND("  %-11s: %d\n", DEPTS[d], perDept[d]);
    APPEND("Employees paid this month: %d\n", cnt);
    APPEND("Total gross salary   : %.2f\nTotal deductions     : %.2f\nTotal net paid       : %.2f\n", gross,ded,net);
    APPEND("Income tax to remit  : %.2f\nSocial security      : %.2f\n", tax,ss);
    if (top) { Employee *e=pr_find(top->empId); APPEND("Highest paid (net)   : %s - %s (%.2f)\n", top->empId, e?e->name:"?", top->net); }
    else APPEND("Highest paid (net)   : n/a\n");
    int best=-1; for (int d=0;d<5;d++) if (deptCost[d]>0 && (best<0||deptCost[d]>deptCost[best])) best=d;
    if (best>=0) APPEND("Highest payroll dept : %s (%.2f)\n", DEPTS[best], deptCost[best]);
    else APPEND("Highest payroll dept : n/a\n");
    APPEND("Employees with more than 3 absent days:\n");
    int any=0;
    for (int i=0;i<g_nPay;i++) if (!strcmp(g_pay[i].month,month) && g_pay[i].absent>3) {
        Employee *e=pr_find(g_pay[i].empId); APPEND("  %s - %s (%d days)\n", g_pay[i].empId, e?e->name:"?", g_pay[i].absent); any=1;
    }
    if (!any) APPEND("  none\n");
    return NULL;
}

int pr_export_text(const char *fn, const char *text) {
    FILE *f = fopen(fn,"w"); if (!f) return 0;
    fputs(text,f); fclose(f); return 1;
}

/* ---------- persistence ---------- */
#define MAGIC 0x50415931u
int pr_save(const char *file) {
    FILE *f = fopen(file,"wb"); if (!f) return 0;
    unsigned m = MAGIC; int c[4] = {g_nEmps,g_nAtt,g_nPay,g_nHist};
    fwrite(&m,sizeof m,1,f); fwrite(c,sizeof c,1,f);
    fwrite(g_emps,sizeof(Employee),g_nEmps,f);  fwrite(g_att,sizeof(Attendance),g_nAtt,f);
    fwrite(g_pay,sizeof(PayrollRec),g_nPay,f);  fwrite(g_hist,sizeof(HistoryRec),g_nHist,f);
    return fclose(f)==0;
}
static int rd(void *dst, size_t sz, int n, FILE *f) { return n==0 || fread(dst,sz,n,f)==(size_t)n; }
int pr_load(const char *file) {
    FILE *f = fopen(file,"rb"); if (!f) return 0;
    unsigned m; int c[4];
    if (fread(&m,sizeof m,1,f)!=1 || m!=MAGIC || fread(c,sizeof c,1,f)!=1 ||
        c[0]<0||c[1]<0||c[2]<0||c[3]<0) { fclose(f); return 0; }
    Employee *e = calloc(c[0]+1,sizeof *e); Attendance *a = calloc(c[1]+1,sizeof *a);
    PayrollRec *p = calloc(c[2]+1,sizeof *p); HistoryRec *h = calloc(c[3]+1,sizeof *h);
    if (!rd(e,sizeof *e,c[0],f)||!rd(a,sizeof *a,c[1],f)||!rd(p,sizeof *p,c[2],f)||!rd(h,sizeof *h,c[3],f)) {
        free(e);free(a);free(p);free(h); fclose(f); return 0;
    }
    fclose(f); pr_free_all();
    g_emps=e; g_nEmps=c[0]; g_att=a; g_nAtt=c[1]; g_pay=p; g_nPay=c[2]; g_hist=h; g_nHist=c[3];
    return 1;
}
void pr_free_all(void) {
    free(g_emps); free(g_att); free(g_pay); free(g_hist);
    g_emps=NULL; g_att=NULL; g_pay=NULL; g_hist=NULL; g_nEmps=g_nAtt=g_nPay=g_nHist=0;
}
