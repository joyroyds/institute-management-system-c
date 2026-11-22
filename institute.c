/*
 * Education Institute Management System
 * --------------------------------------
 * Data structures (each stored in its own binary file = persistent storage):
 *   Student    -> students.dat      (ID, NAME, FEES, PAID flag)
 *   Allocation -> allocations.dat   (studentId -> course)
 *   Result     -> results.dat       (studentId -> grade)
 *   User       -> users.dat         (username + hashed password)
 *
 * Rules enforced:
 *   - Course (DS/AI/ML/Web) can be allotted only AFTER fees are paid.
 *   - A student gets exactly one course.
 *   - Result (Excellent/Good/Fail) can be declared only for allotted students.
 *   - Login required (max 3 attempts). Default user: admin / admin123
 *
 * Compile: gcc -Wall -o institute institute.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STUDENT_FILE "students.dat"
#define ALLOC_FILE   "allocations.dat"
#define RESULT_FILE  "results.dat"
#define USER_FILE    "users.dat"

typedef struct { int id; char name[50]; float fees; int paid; } Student;
typedef struct { int studentId; int courseId; } Allocation;   /* 1..4 */
typedef struct { int studentId; int grade; } Result;          /* 1..3 */
typedef struct { char username[20]; unsigned long hash; } User;

static const char *COURSES[] = {"-", "DS", "AI", "ML", "Web"};
static const char *GRADES[]  = {"Pending", "Excellent", "Good", "Fail"};

/* ---------- input helpers ---------- */
static void readLine(const char *prompt, char *buf, int size) {
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(buf, size, stdin)) { printf("\nInput ended. Exiting.\n"); exit(0); }
    buf[strcspn(buf, "\r\n")] = '\0';
}
static int readInt(const char *prompt) {
    char b[64], *e;
    for (;;) {
        readLine(prompt, b, sizeof b);
        long v = strtol(b, &e, 10);
        if (e != b && *e == '\0') return (int)v;
        printf("  Invalid number, try again.\n");
    }
}
static float readFloat(const char *prompt) {
    char b[64], *e;
    for (;;) {
        readLine(prompt, b, sizeof b);
        float v = strtof(b, &e);
        if (e != b && *e == '\0' && v > 0) return v;
        printf("  Enter a positive amount.\n");
    }
}

/* ---------- generic record I/O (first field of every record is an int key) ---------- */
static int findRecord(const char *file, size_t sz, int key, void *out, long *pos) {
    FILE *f = fopen(file, "rb");
    if (!f) return 0;
    char buf[128]; long i = 0;
    while (fread(buf, sz, 1, f) == 1) {
        if (*(int *)buf == key) {
            if (out) memcpy(out, buf, sz);
            if (pos) *pos = i * (long)sz;
            fclose(f); return 1;
        }
        i++;
    }
    fclose(f); return 0;
}
static void appendRecord(const char *file, const void *rec, size_t sz) {
    FILE *f = fopen(file, "ab");
    if (!f) { perror("fopen"); exit(1); }
    fwrite(rec, sz, 1, f); fclose(f);
}
static void overwriteAt(const char *file, long pos, const void *rec, size_t sz) {
    FILE *f = fopen(file, "r+b");
    if (!f) { perror("fopen"); exit(1); }
    fseek(f, pos, SEEK_SET); fwrite(rec, sz, 1, f); fclose(f);
}
static void removeByKey(const char *file, size_t sz, int key) {
    FILE *f = fopen(file, "rb");
    if (!f) return;
    FILE *t = fopen("tmp.dat", "wb");
    char buf[128];
    while (fread(buf, sz, 1, f) == 1)
        if (*(int *)buf != key) fwrite(buf, sz, 1, t);
    fclose(f); fclose(t);
    remove(file); rename("tmp.dat", file);
}

/* ---------- lookups ---------- */
static int courseOf(int id) { Allocation a; return findRecord(ALLOC_FILE, sizeof a, id, &a, NULL) ? a.courseId : 0; }
static int gradeOf(int id)  { Result r;     return findRecord(RESULT_FILE, sizeof r, id, &r, NULL) ? r.grade : 0; }

/* ---------- authentication ---------- */
static unsigned long hashPwd(const char *s) {          /* djb2 */
    unsigned long h = 5381; int c;
    while ((c = (unsigned char)*s++)) h = h * 33 + c;
    return h;
}
static void ensureDefaultUser(void) {
    FILE *f = fopen(USER_FILE, "rb");
    if (f) { fclose(f); return; }
    User u; memset(&u, 0, sizeof u);
    strcpy(u.username, "admin"); u.hash = hashPwd("admin123");
    appendRecord(USER_FILE, &u, sizeof u);
}
static int login(char *whoOut) {
    for (int attempt = 1; attempt <= 3; attempt++) {
        char un[64], pw[64]; User u; int ok = 0;
        readLine("User ID : ", un, sizeof un);
        readLine("Password: ", pw, sizeof pw);
        FILE *f = fopen(USER_FILE, "rb");
        while (f && fread(&u, sizeof u, 1, f) == 1)
            if (!strcmp(u.username, un) && u.hash == hashPwd(pw)) { ok = 1; break; }
        if (f) fclose(f);
        if (ok) { strcpy(whoOut, un); printf("Login successful. Welcome, %s!\n", un); return 1; }
        printf("Invalid credentials (%d/3).\n", attempt);
    }
    printf("Too many failed attempts. Access denied.\n");
    return 0;
}
static void changePassword(const char *who) {
    char np[64]; User u; long pos;
    readLine("New password (min 4 chars): ", np, sizeof np);
    if (strlen(np) < 4) { printf("Password too short.\n"); return; }
    FILE *f = fopen(USER_FILE, "rb"); long i = 0;
    while (f && fread(&u, sizeof u, 1, f) == 1) {
        if (!strcmp(u.username, who)) {
            pos = i * (long)sizeof u; fclose(f);
            u.hash = hashPwd(np); overwriteAt(USER_FILE, pos, &u, sizeof u);
            printf("Password changed.\n"); return;
        }
        i++;
    }
    if (f) fclose(f);
}

/* ---------- features ---------- */
static void addStudent(void) {
    Student s; memset(&s, 0, sizeof s);
    s.id = readInt("Student ID: ");
    if (s.id <= 0) { printf("ID must be positive.\n"); return; }
    if (findRecord(STUDENT_FILE, sizeof s, s.id, NULL, NULL)) { printf("ID %d already exists.\n", s.id); return; }
    readLine("Name      : ", s.name, sizeof s.name);
    if (!s.name[0]) { printf("Name cannot be empty.\n"); return; }
    s.fees = readFloat("Fees      : ");
    s.paid = 0;
    appendRecord(STUDENT_FILE, &s, sizeof s);
    printf("Student registered.\n");
}
static void printHeader(void) {
    printf("\n%-6s %-20s %-10s %-6s %-6s %-10s\n", "ID", "NAME", "FEES", "PAID", "COURSE", "RESULT");
    printf("---------------------------------------------------------------\n");
}
static void printStudent(const Student *s) {
    printf("%-6d %-20s %-10.2f %-6s %-6s %-10s\n", s->id, s->name, s->fees,
           s->paid ? "Yes" : "No", COURSES[courseOf(s->id)], GRADES[gradeOf(s->id)]);
}
static void viewAll(void) {
    FILE *f = fopen(STUDENT_FILE, "rb"); Student s; int n = 0;
    if (!f) { printf("No students registered.\n"); return; }
    printHeader();
    while (fread(&s, sizeof s, 1, f) == 1) { printStudent(&s); n++; }
    fclose(f);
    if (!n) printf("No students registered.\n"); else printf("Total: %d\n", n);
}
static void searchStudent(void) {
    Student s; int id = readInt("Student ID: ");
    if (!findRecord(STUDENT_FILE, sizeof s, id, &s, NULL)) { printf("Student not found.\n"); return; }
    printHeader(); printStudent(&s);
}
static void updateStudent(void) {
    Student s; long pos; char nm[50];
    int id = readInt("Student ID: ");
    if (!findRecord(STUDENT_FILE, sizeof s, id, &s, &pos)) { printf("Student not found.\n"); return; }
    readLine("New name (blank = keep): ", nm, sizeof nm);
    if (nm[0]) strcpy(s.name, nm);
    if (!s.paid) s.fees = readFloat("New fees: ");
    overwriteAt(STUDENT_FILE, pos, &s, sizeof s);
    printf("Student updated.\n");
}
static void deleteStudent(void) {
    Student s; int id = readInt("Student ID: ");
    if (!findRecord(STUDENT_FILE, sizeof s, id, &s, NULL)) { printf("Student not found.\n"); return; }
    removeByKey(STUDENT_FILE, sizeof(Student), id);
    removeByKey(ALLOC_FILE, sizeof(Allocation), id);
    removeByKey(RESULT_FILE, sizeof(Result), id);
    printf("Student %d and related records deleted.\n", id);
}
static void payFees(void) {
    Student s; long pos; int id = readInt("Student ID: ");
    if (!findRecord(STUDENT_FILE, sizeof s, id, &s, &pos)) { printf("Student not found.\n"); return; }
    if (s.paid) { printf("Fees already paid.\n"); return; }
    printf("Amount due: %.2f\n", s.fees);
    float amt = readFloat("Amount paid: ");
    if (amt < s.fees) { printf("Partial payment not accepted. Fees still pending.\n"); return; }
    s.paid = 1; overwriteAt(STUDENT_FILE, pos, &s, sizeof s);
    printf("Fees paid. Student is eligible for course allotment.\n");
}
static void allocateCourse(void) {
    Student s; int id = readInt("Student ID: ");
    if (!findRecord(STUDENT_FILE, sizeof s, id, &s, NULL)) { printf("Student not found.\n"); return; }
    if (!s.paid) { printf("Fees not paid. Cannot allot a course.\n"); return; }
    if (courseOf(id)) { printf("Already allotted to %s.\n", COURSES[courseOf(id)]); return; }
    int c = readInt("Course (1=DS 2=AI 3=ML 4=Web): ");
    if (c < 1 || c > 4) { printf("Invalid course.\n"); return; }
    Allocation a = { id, c }; appendRecord(ALLOC_FILE, &a, sizeof a);
    printf("%s allotted to %s.\n", s.name, COURSES[c]);
}
static void declareResult(void) {
    Student s; Result r; long pos; int id = readInt("Student ID: ");
    if (!findRecord(STUDENT_FILE, sizeof s, id, &s, NULL)) { printf("Student not found.\n"); return; }
    if (!courseOf(id)) { printf("No course allotted. Cannot declare result.\n"); return; }
    int g = readInt("Grade (1=Excellent 2=Good 3=Fail): ");
    if (g < 1 || g > 3) { printf("Invalid grade.\n"); return; }
    r.studentId = id; r.grade = g;
    if (findRecord(RESULT_FILE, sizeof r, id, NULL, &pos)) overwriteAt(RESULT_FILE, pos, &r, sizeof r);
    else appendRecord(RESULT_FILE, &r, sizeof r);
    printf("Result for %s (%s): %s\n", s.name, COURSES[courseOf(id)], GRADES[g]);
}
static void courseReport(void) {
    int cnt[5][4] = {{0}}; Student s;
    FILE *f = fopen(STUDENT_FILE, "rb");
    if (f) {
        while (fread(&s, sizeof s, 1, f) == 1) {
            int c = courseOf(s.id);
            if (c) cnt[c][gradeOf(s.id)]++;
        }
        fclose(f);
    }
    printf("\n%-8s %-9s %-10s %-6s %-6s %-6s\n", "COURSE", "ENROLLED", "EXCELLENT", "GOOD", "FAIL", "PEND");
    printf("---------------------------------------------------\n");
    for (int c = 1; c <= 4; c++)
        printf("%-8s %-9d %-10d %-6d %-6d %-6d\n", COURSES[c],
               cnt[c][0] + cnt[c][1] + cnt[c][2] + cnt[c][3], cnt[c][1], cnt[c][2], cnt[c][3], cnt[c][0]);
}

/* ---------- main ---------- */
int main(void) {
    char who[20];
    printf("===== INSTITUTE MANAGEMENT SYSTEM =====\n");
    ensureDefaultUser();
    if (!login(who)) return 1;
    for (;;) {
        printf("\n---------------- MENU ----------------\n"
               " 1. Register Student     6. Pay Fees\n"
               " 2. View All Students    7. Allot Course\n"
               " 3. Search Student       8. Declare Result\n"
               " 4. Update Student       9. Course-wise Report\n"
               " 5. Delete Student      10. Change Password\n"
               " 0. Logout\n");
        switch (readInt("Choice: ")) {
            case 1: addStudent(); break;
            case 2: viewAll(); break;
            case 3: searchStudent(); break;
            case 4: updateStudent(); break;
            case 5: deleteStudent(); break;
            case 6: payFees(); break;
            case 7: allocateCourse(); break;
            case 8: declareResult(); break;
            case 9: courseReport(); break;
            case 10: changePassword(who); break;
            case 0: printf("Logged out. Goodbye!\n"); return 0;
            default: printf("Invalid choice.\n");
        }
    }
}
