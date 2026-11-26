/* Employee Payroll Management System - Win32 GUI front end.
   The business rules live in payroll.c; this file only draws windows. */
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "payroll.h"

#define IDC_TAB      101
#define IDC_LIST     102
#define IDC_FILTER   103
#define IDC_FBTN     104
#define IDC_SHOWTERM 105
#define IDC_FLABEL   106
#define ID_BTN0      200
#define ID_EXPORT    150

static HWND hMain, hTab, hList, hFilter, hFBtn, hShowTerm, hFLabel;
static HINSTANCE hInst;
static HFONT hMono;
static char appDir[MAX_PATH], dataPath[MAX_PATH];
static char fId[16], fMonth[8];           /* active filters */
static int *rowMap; static int rowCount;  /* list row -> employee index */

static void app_path(const char *name, char *out) { snprintf(out, MAX_PATH, "%s%s", appDir, name); }
static void err(const char *m)  { MessageBox(hMain, m, "Error", MB_ICONERROR | MB_OK); }
static void info(const char *m) { MessageBox(hMain, m, "Payroll System", MB_ICONINFORMATION | MB_OK); }
static void persist(void) { if (!pr_save(dataPath)) err("Could not write the data file."); }

static int parse_num(const char *s, double *out) {
    char *end; while (*s == ' ') s++;
    if (!*s) return 0;
    *out = strtod(s, &end);
    while (*end == ' ') end++;
    return *end == 0;
}
static int parse_int(const char *s, int *out) {
    double d; if (!parse_num(s, &d) || d != (int)d) return 0;
    *out = (int)d; return 1;
}
static HWND mk(HWND p, const char *cls, const char *txt, DWORD st, DWORD ex, int x, int y, int w, int h, int id) {
    HWND c = CreateWindowEx(ex, cls, txt, WS_CHILD | WS_VISIBLE | st, x, y, w, h, p, (HMENU)(INT_PTR)id, hInst, NULL);
    SendMessage(c, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    return c;
}

/* ================= generic input form (modal) ================= */
typedef struct { const char *label; const char **opts; int nopts; int ro; char value[128]; } Field;
typedef struct { Field *f; int n; int done, ok; HWND ctl[12]; } FormState;

static LRESULT CALLBACK FormProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    FormState *s = (FormState *)GetWindowLongPtr(h, GWLP_USERDATA);
    if (m == WM_CREATE) {
        s = (FormState *)((CREATESTRUCT *)l)->lpCreateParams;
        SetWindowLongPtr(h, GWLP_USERDATA, (LONG_PTR)s);
        for (int i = 0; i < s->n; i++) {
            Field *f = &s->f[i]; int y = 14 + i * 32;
            mk(h, "STATIC", f->label, 0, 0, 12, y + 3, 190, 20, 0);
            if (f->opts) {
                s->ctl[i] = mk(h, "COMBOBOX", "", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 210, y, 200, 160, 300 + i);
                for (int k = 0; k < f->nopts; k++) SendMessage(s->ctl[i], CB_ADDSTRING, 0, (LPARAM)f->opts[k]);
                if (SendMessage(s->ctl[i], CB_SELECTSTRING, -1, (LPARAM)f->value) == CB_ERR) SendMessage(s->ctl[i], CB_SETCURSEL, 0, 0);
            } else {
                s->ctl[i] = mk(h, "EDIT", f->value, WS_TABSTOP | ES_AUTOHSCROLL | (f->ro ? ES_READONLY : 0), WS_EX_CLIENTEDGE, 210, y, 200, 22, 300 + i);
            }
        }
        int by = 24 + s->n * 32;
        mk(h, "BUTTON", "OK", WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 150, by, 80, 28, IDOK);
        mk(h, "BUTTON", "Cancel", WS_TABSTOP, 0, 240, by, 80, 28, IDCANCEL);
        return 0;
    }
    if (!s) return DefWindowProc(h, m, w, l);
    if (m == WM_COMMAND) {
        if (LOWORD(w) == IDOK) {
            for (int i = 0; i < s->n; i++) GetWindowText(s->ctl[i], s->f[i].value, sizeof s->f[i].value);
            s->ok = 1; s->done = 1;
        } else if (LOWORD(w) == IDCANCEL) s->done = 1;
        return 0;
    }
    if (m == WM_CLOSE) { s->done = 1; return 0; }
    return DefWindowProc(h, m, w, l);
}
static void run_modal(HWND win, int *done) {
    MSG msg;
    EnableWindow(hMain, FALSE); ShowWindow(win, SW_SHOW); UpdateWindow(win);
    while (!*done && GetMessage(&msg, NULL, 0, 0)) {
        if (!IsDialogMessage(win, &msg)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    }
    EnableWindow(hMain, TRUE); DestroyWindow(win); SetForegroundWindow(hMain);
}
static int form_dialog(const char *title, Field *f, int n) {
    FormState s; memset(&s, 0, sizeof s); s.f = f; s.n = n;
    RECT r = {0, 0, 430, 70 + n * 32}; DWORD st = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRectEx(&r, st, FALSE, WS_EX_DLGMODALFRAME);
    RECT pr; GetWindowRect(hMain, &pr);
    int W = r.right - r.left, H = r.bottom - r.top;
    HWND win = CreateWindowEx(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, "FormWnd", title, st,
        pr.left + ((pr.right - pr.left) - W) / 2, pr.top + 80, W, H, hMain, NULL, hInst, &s);
    run_modal(win, &s.done);
    return s.ok;
}

/* ================= text viewer (payslip / report / breakdown) ================= */
typedef struct { const char *text; const char *file; int done; } ViewState;
static LRESULT CALLBACK ViewProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    ViewState *s = (ViewState *)GetWindowLongPtr(h, GWLP_USERDATA);
    if (m == WM_CREATE) {
        s = (ViewState *)((CREATESTRUCT *)l)->lpCreateParams;
        SetWindowLongPtr(h, GWLP_USERDATA, (LONG_PTR)s);
        size_t len = strlen(s->text); char *crlf = malloc(len * 2 + 1), *q = crlf;
        for (const char *p = s->text; *p; p++) { if (*p == '\n') *q++ = '\r'; *q++ = *p; }
        *q = 0;
        HWND e = mk(h, "EDIT", crlf, ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL, WS_EX_CLIENTEDGE, 10, 10, 600, 410, 0);
        SendMessage(e, WM_SETFONT, (WPARAM)hMono, TRUE);
        free(crlf);
        if (s->file) mk(h, "BUTTON", "Export to text file", WS_TABSTOP, 0, 350, 432, 150, 28, ID_EXPORT);
        mk(h, "BUTTON", "Close", WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 510, 432, 100, 28, IDCANCEL);
        return 0;
    }
    if (!s) return DefWindowProc(h, m, w, l);
    if (m == WM_COMMAND) {
        if (LOWORD(w) == IDCANCEL) s->done = 1;
        else if (LOWORD(w) == ID_EXPORT) {
            char path[MAX_PATH], msg[MAX_PATH + 40]; app_path(s->file, path);
            if (pr_export_text(path, s->text)) { snprintf(msg, sizeof msg, "Saved to:\n%s", path); MessageBox(h, msg, "Export", MB_ICONINFORMATION); }
            else MessageBox(h, "Could not write the file.", "Error", MB_ICONERROR);
        }
        return 0;
    }
    if (m == WM_CLOSE) { s->done = 1; return 0; }
    return DefWindowProc(h, m, w, l);
}
static void text_viewer(const char *title, const char *text, const char *file) {
    ViewState s = { text, file, 0 };
    RECT r = {0, 0, 620, 470}; DWORD st = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRectEx(&r, st, FALSE, WS_EX_DLGMODALFRAME);
    RECT pr; GetWindowRect(hMain, &pr);
    int W = r.right - r.left, H = r.bottom - r.top;
    HWND win = CreateWindowEx(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, "ViewWnd", title, st,
        pr.left + ((pr.right - pr.left) - W) / 2, pr.top + 40, W, H, hMain, NULL, hInst, &s);
    run_modal(win, &s.done);
}

/* ================= list view (the tables) ================= */
static void set_cols(const char **names, const int *wd, int n) {
    while (ListView_DeleteColumn(hList, 0)) ;
    LVCOLUMN c; memset(&c, 0, sizeof c); c.mask = LVCF_TEXT | LVCF_WIDTH;
    for (int i = 0; i < n; i++) { c.pszText = (char *)names[i]; c.cx = wd[i]; ListView_InsertColumn(hList, i, &c); }
}
static void put(int row, int col, const char *t) {
    if (col == 0) { LVITEM it; memset(&it, 0, sizeof it); it.mask = LVIF_TEXT; it.iItem = row; it.pszText = (char *)t; ListView_InsertItem(hList, &it); }
    else ListView_SetItemText(hList, row, col, (char *)t);
}
static int match(const char *id, const char *month) {
    return (!fId[0] || !strcmp(fId, id)) && (!fMonth[0] || !strcmp(fMonth, month));
}
static void refresh(void) {
    char b[64]; int tab = TabCtrl_GetCurSel(hTab), row = 0;
    SendMessage(hList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(hList);
    free(rowMap); rowMap = NULL; rowCount = 0;
    if (tab == 0) {
        const char *n[] = {"Employee ID","Full name","Department","Job title","Type","Basic salary","Status"};
        int w[] = {90,200,110,170,90,110,100}; set_cols(n, w, 7);
        int showT = SendMessage(hShowTerm, BM_GETCHECK, 0, 0) == BST_CHECKED;
        rowMap = malloc((g_nEmps + 1) * sizeof(int));
        for (int i = 0; i < g_nEmps; i++) {
            Employee *e = &g_emps[i];
            if (!showT && !strcmp(e->status, "Terminated")) continue;
            put(row, 0, e->id); put(row, 1, e->name); put(row, 2, e->dept); put(row, 3, e->title);
            put(row, 4, e->type); snprintf(b, sizeof b, "%.2f", e->basic); put(row, 5, b); put(row, 6, e->status);
            rowMap[row++] = i;
        }
        rowCount = row;
    } else if (tab == 1) {
        const char *n[] = {"Employee ID","Name","Pay month","Days worked","Days absent","Overtime hours"};
        int w[] = {100,220,100,110,110,120}; set_cols(n, w, 6);
        for (int i = 0; i < g_nAtt; i++) {
            Attendance *a = &g_att[i]; if (!match(a->empId, a->month)) continue;
            Employee *e = pr_find(a->empId);
            put(row, 0, a->empId); put(row, 1, e ? e->name : "?"); put(row, 2, a->month);
            snprintf(b, sizeof b, "%d", a->worked); put(row, 3, b);
            snprintf(b, sizeof b, "%d", a->absent); put(row, 4, b);
            snprintf(b, sizeof b, "%.1f", a->overtime); put(row, 5, b); row++;
        }
    } else if (tab == 2) {
        const char *n[] = {"Employee ID","Month","Gross","Income tax","Social sec.","Absence ded.","Loan","Total deductions","Net salary"};
        int w[] = {90,80,100,100,100,100,80,120,110}; set_cols(n, w, 9);
        for (int i = 0; i < g_nPay; i++) {
            PayrollRec *p = &g_pay[i]; if (!match(p->empId, p->month)) continue;
            double v[] = {p->gross, p->tax, p->ss, p->absDed, p->loan, p->totalDed, p->net};
            put(row, 0, p->empId); put(row, 1, p->month);
            for (int k = 0; k < 7; k++) { snprintf(b, sizeof b, "%.2f", v[k]); put(row, 2 + k, b); }
            row++;
        }
    } else {
        const char *n[] = {"Date / time","Operation","Employee ID","Month","Details"};
        int w[] = {160,130,100,80,400}; set_cols(n, w, 5);
        for (int i = 0; i < g_nHist; i++) {
            HistoryRec *h = &g_hist[i]; if (!match(h->empId, h->month)) continue;
            put(row, 0, h->stamp); put(row, 1, h->action); put(row, 2, h->empId); put(row, 3, h->month); put(row, 4, h->detail); row++;
        }
    }
    SendMessage(hList, WM_SETREDRAW, TRUE, 0); InvalidateRect(hList, NULL, TRUE);
}
static void set_tab(int t) {
    TabCtrl_SetCurSel(hTab, t);
    int isEmp = (t == 0);
    ShowWindow(hShowTerm, isEmp ? SW_SHOW : SW_HIDE);
    ShowWindow(hFilter, isEmp ? SW_HIDE : SW_SHOW);
    ShowWindow(hFBtn, isEmp ? SW_HIDE : SW_SHOW);
    if (isEmp) { SetWindowText(hFLabel, "Grey = terminated    Blue = intern    Red = basic salary below the minimum wage (30000)"); MoveWindow(hFLabel, 10, 80, 760, 20, TRUE); }
    else { SetWindowText(hFLabel, "Filter by employee ID or month (MM-YYYY):"); MoveWindow(hFLabel, 10, 80, 270, 20, TRUE); }
    refresh();
}
static int sel_id(char *out) {
    if (TabCtrl_GetCurSel(hTab) != 0) return 0;
    int i = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
    if (i < 0) return 0;
    ListView_GetItemText(hList, i, 0, out, 16); return 1;
}
static void set_field(Field *f, const char *v) { snprintf(f->value, sizeof f->value, "%s", v); }

/* ================= actions (one per button) ================= */
static void act_add(void) {
    Field f[] = {{"Employee ID"}, {"Full name"}, {"National ID"}, {"Department", DEPTS, 5}, {"Job title"},
                 {"Employment type", TYPES, 3}, {"Date of joining (YYYY-MM-DD)"}, {"Bank account number"}, {"Basic salary"}};
    if (!form_dialog("Add Employee", f, 9)) return;
    Employee e; memset(&e, 0, sizeof e); double basic;
    snprintf(e.id, sizeof e.id, "%s", f[0].value);     snprintf(e.name, sizeof e.name, "%s", f[1].value);
    snprintf(e.nid, sizeof e.nid, "%s", f[2].value);   snprintf(e.dept, sizeof e.dept, "%s", f[3].value);
    snprintf(e.title, sizeof e.title, "%s", f[4].value); snprintf(e.type, sizeof e.type, "%s", f[5].value);
    snprintf(e.joined, sizeof e.joined, "%s", f[6].value); snprintf(e.bank, sizeof e.bank, "%s", f[7].value);
    if (!parse_num(f[8].value, &basic)) { err("Basic salary must be a number."); return; }
    e.basic = basic;
    const char *r = pr_add_employee(&e);
    if (r) { err(r); return; }
    persist(); set_tab(0); info("Employee saved successfully.");
}
static void act_update(void) {
    char id[16]; if (!sel_id(id)) { err("Select an employee in the Employees table first."); return; }
    Employee *e = pr_find(id); char b[32]; snprintf(b, sizeof b, "%.2f", e->basic);
    Field f[] = {{"Employee ID", NULL, 0, 1}, {"Department", DEPTS, 5}, {"Job title"}, {"Employment type", TYPES, 3}, {"Basic salary"}, {"Status", STATUSES, 3}};
    set_field(&f[0], e->id); set_field(&f[1], e->dept); set_field(&f[2], e->title);
    set_field(&f[3], e->type); set_field(&f[4], b); set_field(&f[5], e->status);
    if (!form_dialog("Update Employee", f, 6)) return;
    double basic; if (!parse_num(f[4].value, &basic)) { err("Basic salary must be a number."); return; }
    const char *r = pr_update_employee(id, f[1].value, f[2].value, f[3].value, basic, f[5].value);
    if (r) { err(r); return; }
    persist(); refresh(); info("Employee updated.");
}
static void act_remove(void) {
    Field f[] = {{"Employee ID to remove"}}; char id[16];
    if (sel_id(id)) set_field(&f[0], id);
    if (!form_dialog("Remove Employee", f, 1)) return;
    Employee *e = pr_find(f[0].value);
    if (!e) { err("Employee not found"); return; }
    char q[160]; snprintf(q, sizeof q, "Mark %s (%s) as Terminated?\nRecords are kept.", e->name, e->id);
    if (MessageBox(hMain, q, "Confirm removal", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    const char *r = pr_terminate(f[0].value);
    if (r) { err(r); return; }
    persist(); refresh(); info("Employee marked as Terminated.");
}
static void act_allow(void) {
    char id[16]; if (!sel_id(id)) { err("Select an employee in the Employees table first."); return; }
    Employee *e = pr_find(id); char t[32], md[32];
    snprintf(t, sizeof t, "%.2f", e->transport); snprintf(md, sizeof md, "%.2f", e->medical);
    Field f[] = {{"Employee ID", NULL, 0, 1}, {"Transport allowance"}, {"Medical allowance"}};
    set_field(&f[0], id); set_field(&f[1], t); set_field(&f[2], md);
    if (!form_dialog("Manage Allowances", f, 3)) return;
    double tr, me;
    if (!parse_num(f[1].value, &tr) || !parse_num(f[2].value, &me)) { err("Amounts must be numbers."); return; }
    const char *r = pr_set_allowances(id, tr, me);
    if (r) { err(r); return; }
    persist(); info("Allowances saved.\nHousing (15% of basic) is added automatically.");
}
static void act_att(void) {
    Field f[] = {{"Employee ID"}, {"Pay month (MM-YYYY)"}, {"Days worked"}, {"Days absent"}, {"Overtime hours (0-60)"}};
    char id[16]; if (sel_id(id)) set_field(&f[0], id);
    if (!form_dialog("Record Attendance", f, 5)) return;
    int wk, ab; double ot;
    if (!parse_int(f[2].value, &wk) || !parse_int(f[3].value, &ab)) { err("Days worked and days absent must be whole numbers."); return; }
    if (!parse_num(f[4].value, &ot)) { err("Overtime hours must be a number."); return; }
    const char *r = pr_add_attendance(f[0].value, f[1].value, wk, ab, ot);
    if (r) { err(r); return; }
    persist(); fId[0] = fMonth[0] = 0; SetWindowText(hFilter, ""); set_tab(1); info("Attendance record saved.");
}
static void act_calc(void) {
    Field f[] = {{"Employee ID"}, {"Pay month (MM-YYYY)"}, {"Performance bonus % (0-20)"}, {"Loan repayment"}};
    char id[16]; if (sel_id(id)) set_field(&f[0], id);
    set_field(&f[2], "0"); set_field(&f[3], "0");
    if (!form_dialog("Calculate Salary", f, 4)) return;
    double bonus, loan;
    if (!parse_num(f[2].value, &bonus) || !parse_num(f[3].value, &loan)) { err("Bonus % and loan must be numbers."); return; }
    PayrollRec p; const char *r = pr_calculate(f[0].value, f[1].value, bonus, loan, &p);
    if (r) { err(r); return; }
    persist();
    Employee *e = pr_find(p.empId); char t[1500];
    snprintf(t, sizeof t,
        "Employee : %s (%s)\nMonth    : %s\n\n"
        "EARNINGS\n  Basic salary        %12.2f\n  Housing (15%%)       %12.2f\n  Transport           %12.2f\n"
        "  Medical             %12.2f\n  Performance bonus   %12.2f\n  Overtime pay        %12.2f\n"
        "  GROSS SALARY        %12.2f\n\n"
        "DEDUCTIONS\n  Income tax          %12.2f\n  Social security     %12.2f\n  Absence deduction   %12.2f\n"
        "  Loan repayment      %12.2f\n  TOTAL DEDUCTIONS    %12.2f\n\n"
        "NET SALARY            %12.2f\n\nThe payroll record has been saved.\n",
        e->name, e->id, p.month, p.basic, p.housing, p.transport, p.medical, p.bonus, p.otPay, p.gross,
        p.tax, p.ss, p.absDed, p.loan, p.totalDed, p.net);
    fId[0] = fMonth[0] = 0; SetWindowText(hFilter, ""); set_tab(2);
    text_viewer("Salary breakdown", t, NULL);
}
static void act_payslip(void) {
    Field f[] = {{"Employee ID"}, {"Pay month (MM-YYYY)"}};
    char id[16]; if (sel_id(id)) set_field(&f[0], id);
    if (!form_dialog("Generate Payslip", f, 2)) return;
    char buf[2048], file[64];
    const char *r = pr_payslip_text(f[0].value, f[1].value, buf, sizeof buf);
    if (r) { err(r); return; }
    persist(); snprintf(file, sizeof file, "payslip_%s_%s.txt", f[0].value, f[1].value);
    text_viewer("Payslip", buf, file);
    if (TabCtrl_GetCurSel(hTab) == 3) refresh();
}
static void act_deduct(void) {
    Field f[] = {{"Employee ID"}, {"Pay month (MM-YYYY)"}};
    char id[16]; if (sel_id(id)) set_field(&f[0], id);
    if (!form_dialog("View Deductions", f, 2)) return;
    if (!pr_find_pay(f[0].value, f[1].value)) { err("No payroll record for this employee and month"); return; }
    snprintf(fId, sizeof fId, "%s", f[0].value); snprintf(fMonth, sizeof fMonth, "%s", f[1].value);
    SetWindowText(hFilter, ""); set_tab(2);
}
static void act_report(void) {
    Field f[] = {{"Report month (MM-YYYY)"}};
    if (!form_dialog("Payroll Report", f, 1)) return;
    char buf[3000], file[64];
    const char *r = pr_report_text(f[0].value, buf, sizeof buf);
    if (r) { err(r); return; }
    snprintf(file, sizeof file, "report_%s.txt", f[0].value);
    text_viewer("Payroll report", buf, file);
}
static void act_filter(void) {
    char t[64]; GetWindowText(hFilter, t, sizeof t);
    char *p = t; while (*p == ' ') p++;
    fId[0] = fMonth[0] = 0;
    if (strlen(p) == 7 && p[2] == '-') snprintf(fMonth, sizeof fMonth, "%s", p);
    else if (*p) snprintf(fId, sizeof fId, "%s", p);
    refresh();
}

/* ================= main window ================= */
static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        hMain = h;
        const char *names[] = {"Add Employee","Update","Remove","Allowances","Attendance","Calculate Salary","Payslip","Deductions","Report"};
        for (int i = 0; i < 9; i++) mk(h, "BUTTON", names[i], WS_TABSTOP, 0, 10 + i * 122, 8, 118, 30, ID_BTN0 + i);
        hTab = mk(h, WC_TABCONTROL, "", WS_CLIPSIBLINGS, 0, 10, 46, 1000, 26, IDC_TAB);
        const char *tabs[] = {"Employees","Attendance","Payroll & deductions","History"};
        TCITEM ti; memset(&ti, 0, sizeof ti); ti.mask = TCIF_TEXT;
        for (int i = 0; i < 4; i++) { ti.pszText = (char *)tabs[i]; TabCtrl_InsertItem(hTab, i, &ti); }
        hFLabel = mk(h, "STATIC", "", 0, 0, 10, 80, 760, 20, IDC_FLABEL);
        hFilter = mk(h, "EDIT", "", WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 285, 77, 160, 24, IDC_FILTER);
        hFBtn = mk(h, "BUTTON", "Apply / clear filter", WS_TABSTOP, 0, 455, 76, 140, 26, IDC_FBTN);
        hShowTerm = mk(h, "BUTTON", "Show terminated employees", WS_TABSTOP | BS_AUTOCHECKBOX, 0, 800, 78, 220, 22, IDC_SHOWTERM);
        hList = mk(h, WC_LISTVIEW, "", WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, WS_EX_CLIENTEDGE, 10, 108, 1000, 400, IDC_LIST);
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        set_tab(0);
        return 0;
    }
    case WM_SIZE: {
        int W = LOWORD(l), H = HIWORD(l);
        MoveWindow(hTab, 10, 46, W - 20, 26, TRUE);
        MoveWindow(hList, 10, 108, W - 20, H - 118, TRUE);
        MoveWindow(hShowTerm, W - 240, 78, 220, 22, TRUE);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        switch (id) {
            case ID_BTN0 + 0: act_add(); break;     case ID_BTN0 + 1: act_update(); break;
            case ID_BTN0 + 2: act_remove(); break;  case ID_BTN0 + 3: act_allow(); break;
            case ID_BTN0 + 4: act_att(); break;     case ID_BTN0 + 5: act_calc(); break;
            case ID_BTN0 + 6: act_payslip(); break; case ID_BTN0 + 7: act_deduct(); break;
            case ID_BTN0 + 8: act_report(); break;
            case IDC_FBTN: act_filter(); break;
            case IDC_SHOWTERM: refresh(); break;
        }
        return 0;
    }
    case WM_NOTIFY: {
        NMHDR *nh = (NMHDR *)l;
        if (nh->hwndFrom == hTab && nh->code == TCN_SELCHANGE) {
            fId[0] = fMonth[0] = 0; SetWindowText(hFilter, ""); set_tab(TabCtrl_GetCurSel(hTab));
        } else if (nh->hwndFrom == hList && nh->code == NM_CUSTOMDRAW) {
            NMLVCUSTOMDRAW *cd = (NMLVCUSTOMDRAW *)l;
            if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                int i = (int)cd->nmcd.dwItemSpec;
                if (TabCtrl_GetCurSel(hTab) == 0 && rowMap && i >= 0 && i < rowCount) {
                    Employee *e = &g_emps[rowMap[i]];
                    if (!strcmp(e->status, "Terminated")) cd->clrText = RGB(130, 130, 130);
                    else if (!strcmp(e->type, "Intern")) cd->clrText = RGB(0, 60, 220);
                    else if (e->basic < MIN_WAGE)        cd->clrText = RGB(210, 0, 0);
                }
                return CDRF_NEWFONT;
            }
            return CDRF_DODEFAULT;
        }
        return 0;
    }
    case WM_CLOSE:
        pr_save(dataPath); DestroyWindow(h); return 0;
    case WM_DESTROY:
        PostQuitMessage(0); return 0;
    }
    return DefWindowProc(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show) {
    (void)hp; (void)cmd; hInst = hi;
    INITCOMMONCONTROLSEX ic = { sizeof ic, ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&ic);
    GetModuleFileName(NULL, appDir, MAX_PATH);
    char *sl = strrchr(appDir, '\\'); if (sl) sl[1] = 0;
    app_path(DATA_FILE, dataPath);
    pr_load(dataPath);                       /* missing file is fine */
    hMono = CreateFont(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, FIXED_PITCH, "Courier New");

    WNDCLASS wc; memset(&wc, 0, sizeof wc);
    wc.hInstance = hi; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "PayMain"; wc.lpfnWndProc = MainProc; wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClass(&wc);
    wc.lpszClassName = "FormWnd"; wc.lpfnWndProc = FormProc; wc.hIcon = NULL; RegisterClass(&wc);
    wc.lpszClassName = "ViewWnd"; wc.lpfnWndProc = ViewProc; RegisterClass(&wc);

    HWND w = CreateWindow("PayMain", "Employee Payroll Management System", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                          CW_USEDEFAULT, CW_USEDEFAULT, 1130, 680, NULL, NULL, hi, NULL);
    ShowWindow(w, show); UpdateWindow(w);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return (int)msg.wParam;
}
