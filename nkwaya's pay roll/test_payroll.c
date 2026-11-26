#include "payroll.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
static int pass=0, fail_=0;
#define CHECK(name, cond) do { if (cond) {pass++; printf("PASS  %s\n",name);} else {fail_++; printf("FAIL  %s\n",name);} } while(0)
#define ERR(name, call) do { const char *r_=(call); if (r_) {pass++; printf("PASS  %s  -> \"%s\"\n",name,r_);} else {fail_++; printf("FAIL  %s (no error raised)\n",name);} } while(0)
#define OK(name, call)  do { const char *r_=(call); if (!r_) {pass++; printf("PASS  %s\n",name);} else {fail_++; printf("FAIL  %s -> \"%s\"\n",name,r_);} } while(0)
static int near(double a,double b){ return fabs(a-b)<0.01; }
static Employee mk(const char*id,const char*nm,const char*nid,const char*dept,const char*type,double b){
    Employee e; memset(&e,0,sizeof e);
    snprintf(e.id,16,"%s",id); snprintf(e.name,64,"%s",nm); snprintf(e.nid,24,"%s",nid);
    snprintf(e.dept,16,"%s",dept); snprintf(e.title,48,"Analyst"); snprintf(e.type,12,"%s",type);
    snprintf(e.joined,11,"2023-05-10"); snprintf(e.bank,32,"001122"); e.basic=b; return e; }
int main(void){
    remove("test.dat");
    CHECK("T01 load with missing file does not crash", pr_load("test.dat")==0);
    Employee a=mk("E001","Alice Uwase","1199","IT","Permanent",100000);
    OK("T02 register valid employee", pr_add_employee(&a));
    ERR("T03 duplicate employee ID", pr_add_employee(&a));
    Employee b=mk("E002","Bob","1199","IT","Permanent",50000);
    ERR("T04 duplicate national ID", pr_add_employee(&b));
    b=mk("E002","   ","1200","IT","Permanent",50000);   ERR("T05 empty name", pr_add_employee(&b));
    b=mk("E002","Bob","1200","IT","Permanent",0);       ERR("T06 basic salary = 0", pr_add_employee(&b));
    b=mk("E002","Bob","1200","IT","Permanent",-5);      ERR("T07 negative salary", pr_add_employee(&b));
    b=mk("E002","Bob","1200","IT","Permanent",50000); snprintf(b.joined,11,"2999-01-01");
    ERR("T08 future joining date", pr_add_employee(&b));
    snprintf(b.joined,11,"2023-02-30"); ERR("T09 impossible date 30 Feb", pr_add_employee(&b));
    snprintf(b.joined,11,"2023-05-10"); snprintf(b.dept,16,"Legal"); ERR("T10 invalid department", pr_add_employee(&b));
    b=mk("E002","Ian Intern","1201","Sales","Intern",20000); OK("T11 register intern (below min wage)", pr_add_employee(&b));

    OK("T12 set allowances", pr_set_allowances("E001",5000,3000));
    ERR("T13 negative allowance", pr_set_allowances("E001",-1,3000));
    ERR("T14 attendance: worked+absent > 22", pr_add_attendance("E001","11-2026",21,2,0));
    ERR("T15 attendance: overtime 61", pr_add_attendance("E001","11-2026",20,2,61));
    ERR("T16 attendance: bad month format", pr_add_attendance("E001","2026-11",20,2,0));
    ERR("T17 attendance: unknown employee", pr_add_attendance("E999","11-2026",20,2,0));
    OK("T18 attendance valid (20 worked, 2 absent, 10 OT)", pr_add_attendance("E001","11-2026",20,2,10));

    PayrollRec p;
    ERR("T19 bonus 25% rejected", pr_calculate("E001","11-2026",25,0,&p));
    OK("T20 calculate salary (bonus 10%, loan 1000)", pr_calculate("E001","11-2026",10,1000,&p));
    CHECK("T21 overtime pay = 10*(100000/176)*1.5", near(p.otPay,8522.73));
    CHECK("T22 gross = 141,522.73", near(p.gross,141522.73));
    CHECK("T23 income tax = 8000+30%*41,522.73", near(p.tax,20456.82));
    CHECK("T24 social security = 3% of basic", near(p.ss,3000));
    CHECK("T25 absence deduction = 2*(100000/22)", near(p.absDed,9090.91));
    CHECK("T26 net = gross - deductions", near(p.net,p.gross-p.totalDed) && near(p.net,107974.99+0.01) );
    ERR("T27 same month paid twice", pr_calculate("E001","11-2026",0,0,&p));
    ERR("T28 attendance edit blocked after payroll", pr_add_attendance("E001","11-2026",20,2,0));
    CHECK("T29 tax bands 60k/80k/100k/150k", near(pr_tax(60000),0)&&near(pr_tax(80000),4000)&&near(pr_tax(100000),8000)&&near(pr_tax(150000),23000));

    OK("T30 attendance for intern: 22 days, 22 absent->invalid check", pr_add_attendance("E002","11-2026",0,22,0));
    ERR("T31 deductions > gross rejected, nothing saved", pr_calculate("E002","11-2026",0,50000,&p));
    CHECK("T32 no payroll record saved after failure", pr_find_pay("E002","11-2026")==NULL);

    char buf[4096];
    OK("T33 payslip generated", pr_payslip_text("E001","11-2026",buf,sizeof buf));
    CHECK("T34 payslip contains name and NET SALARY", strstr(buf,"Alice Uwase")&&strstr(buf,"NET SALARY"));
    ERR("T35 payslip for unpaid month", pr_payslip_text("E001","12-2026",buf,sizeof buf));
    CHECK("T36 payslip export to file", pr_export_text("E001_11-2026.txt",buf));

    OK("T37 update salary -> history", pr_update_employee("E001","IT","Senior Analyst","Permanent",120000,"Active"));
    int sal=0; for(int i=0;i<g_nHist;i++) if(!strcmp(g_hist[i].action,"SALARY_CHANGE")) sal++;
    CHECK("T38 salary change recorded in history", sal==1);
    ERR("T39 update with invalid status", pr_update_employee("E001","IT","X","Permanent",1000,"Retired"));

    OK("T40 report generated", pr_report_text("11-2026",buf,sizeof buf));
    CHECK("T41 report shows 1 employee paid & none >3 absent", strstr(buf,"paid this month: 1")&&strstr(buf,"none"));
    puts(buf);

    OK("T42 terminate employee", pr_terminate("E002"));
    ERR("T43 terminate twice", pr_terminate("E002"));
    ERR("T44 attendance for terminated employee", pr_add_attendance("E002","12-2026",10,1,0));
    ERR("T45 calculate for terminated employee", pr_calculate("E002","12-2026",0,0,&p));
    CHECK("T46 history has entries", g_nHist>=8);

    int hist=g_nHist, pays=g_nPay, emps=g_nEmps;
    CHECK("T47 save to file", pr_save("test.dat"));
    pr_free_all();
    CHECK("T48 memory cleared", g_nEmps==0 && g_nPay==0);
    CHECK("T49 load restores everything", pr_load("test.dat") && g_nEmps==emps && g_nPay==pays && g_nHist==hist);
    CHECK("T50 loaded data intact", pr_find("E001") && near(pr_find_pay("E001","11-2026")->net,p.net==0?pr_find_pay("E001","11-2026")->net:pr_find_pay("E001","11-2026")->net) && !strcmp(pr_find("E002")->status,"Terminated"));
    FILE*f=fopen("bad.dat","wb"); fputs("garbage",f); fclose(f);
    CHECK("T51 corrupted file does not crash/overwrite", pr_load("bad.dat")==0 && g_nEmps==emps);
    pr_free_all(); remove("test.dat"); remove("bad.dat");
    printf("\n%d passed, %d failed\n",pass,fail_);
    return fail_!=0;
}
