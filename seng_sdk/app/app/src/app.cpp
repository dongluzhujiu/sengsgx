#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <unistd.h>
#include <pwd.h>
#include <libgen.h>
#include <stdlib.h>
#include <pthread.h>

#include <sys/time.h>

#include <signal.h>

# define MAX_PATH FILENAME_MAX

#include <sgx_urts.h>
#include <sgx_uswitchless.h>

#include "app.h"

#include "app_enclave_u.h"

void *run_enclave_test(void *);

// Based on SDK Sample code

/* Global EID shared by multiple threads */
sgx_enclave_id_t global_eid = 0;

typedef struct _sgx_errlist_t {
    sgx_status_t err;
    const char *msg;
    const char *sug; /* Suggestion */
} sgx_errlist_t;

/* Error code returned by sgx_create_enclave */
static sgx_errlist_t sgx_errlist[] = {
    {
        SGX_ERROR_UNEXPECTED,
        "Unexpected error occurred.",
        NULL
    },
    {
        SGX_ERROR_INVALID_PARAMETER,
        "Invalid parameter.",
        NULL
    },
    {
        SGX_ERROR_OUT_OF_MEMORY,
        "Out of memory.",
        NULL
    },
    {
        SGX_ERROR_ENCLAVE_LOST,
        "Power transition occurred.",
        "Please refer to the sample \"PowerTransition\" for details."
    },
    {
        SGX_ERROR_INVALID_ENCLAVE,
        "Invalid enclave image.",
        NULL
    },
    {
        SGX_ERROR_INVALID_ENCLAVE_ID,
        "Invalid enclave identification.",
        NULL
    },
    {
        SGX_ERROR_INVALID_SIGNATURE,
        "Invalid enclave signature.",
        NULL
    },
    {
        SGX_ERROR_OUT_OF_EPC,
        "Out of EPC memory.",
        NULL
    },
    {
        SGX_ERROR_NO_DEVICE,
        "Invalid Intel® Software Guard Extensions device.",
        "Please make sure Intel® Software Guard Extensions module is enabled in the BIOS, and install Intel® Software Guard Extensions driver afterwards."
    },
    {
        SGX_ERROR_MEMORY_MAP_CONFLICT,
        "Memory map conflicted.",
        NULL
    },
    {
        SGX_ERROR_INVALID_METADATA,
        "Invalid enclave metadata.",
        NULL
    },
    {
        SGX_ERROR_DEVICE_BUSY,
        "Intel® Software Guard Extensions device was busy.",
        NULL
    },
    {
        SGX_ERROR_INVALID_VERSION,
        "Enclave version was invalid.",
        NULL
    },
    {
        SGX_ERROR_INVALID_ATTRIBUTE,
        "Enclave was not authorized.",
        NULL
    },
    {
        SGX_ERROR_ENCLAVE_FILE_ACCESS,
        "Can't open enclave file.",
        NULL
    },
};

/* Check error conditions for loading enclave */
void print_error_message(sgx_status_t ret)
{
    size_t idx = 0;
    size_t ttl = sizeof sgx_errlist/sizeof sgx_errlist[0];

    for (idx = 0; idx < ttl; idx++) {
        if(ret == sgx_errlist[idx].err) {
            if(NULL != sgx_errlist[idx].sug)
                printf("Info: %s\n", sgx_errlist[idx].sug);
            printf("Error: %s\n", sgx_errlist[idx].msg);
            break;
        }
    }
    
    if (idx == ttl)
        printf("Error: Unexpected error occurred [0x%x].\n", ret);
}

void call_ocall_bench(bool switchless) {
    if(!switchless) printf("[Enclave] Normal OCALL bench\n");
    else printf("[Enclave] Switchless OCALL bench\n");
    
    struct timeval tval_before, tval_after, tval_result;
    gettimeofday(&tval_before, NULL);

    sgx_status_t status = bench_ocall(global_eid, switchless ? ETRUE : EFALSE);
    if (status != SGX_SUCCESS) printf("Call to bench_ocall has failed.\n");

    gettimeofday(&tval_after, NULL);
    timersub(&tval_after, &tval_before, &tval_result);
    printf("Time elapsed: %ld.%06ld seconds\n", (long int)tval_result.tv_sec, (long int)tval_result.tv_usec);
}

void bench_ecall(bool switchless) {
    if(!switchless) printf("[Enclave] Normal ECALL bench\n");
    else printf("[Enclave] Switchless ECALL bench\n");
    
    struct timeval tval_before, tval_after, tval_result;
    gettimeofday(&tval_before, NULL);
    int retval;

    if (!switchless) {
        for (int i=0; i<500000; i++) {
            sgx_status_t status = bench_empty_ecall(global_eid, &retval);
            if (status != SGX_SUCCESS) {
                printf("Call to bench_empty_ecall has failed.\n");
            }
        }
    } else {
        for (int i=0; i<500000; i++) {
            sgx_status_t status = bench_switchless_empty_ecall(global_eid, &retval);
            if (status != SGX_SUCCESS) {
                printf("Call to bench_switchless_empty_ecall has failed.\n");
            }
        }
    }
    gettimeofday(&tval_after, NULL);
    timersub(&tval_after, &tval_before, &tval_result);
    printf("Time elapsed: %ld.%06ld seconds\n", (long int)tval_result.tv_sec, (long int)tval_result.tv_usec);
}

/* Initialize the enclave:
 *   Step 1: retrive the launch token saved by last transaction
 *   Step 2: call sgx_create_enclave to initialize an enclave instance
 *   Step 3: save the launch token if it is updated
 */
int initialize_enclave(const sgx_uswitchless_config_t* us_config)
{
    char token_path[MAX_PATH] = {'\0'};
    sgx_launch_token_t token = {0};
    sgx_status_t ret = SGX_ERROR_UNEXPECTED;
    int updated = 0;
    /* Step 1: retrive the launch token saved by last transaction */

    /* try to get the token saved in $HOME */
    const char *home_dir = getpwuid(getuid())->pw_dir;
    if (home_dir != NULL && 
        (strlen(home_dir)+strlen("/")+sizeof(TOKEN_FILENAME)+1) <= MAX_PATH) {
        /* compose the token path */
        strncpy(token_path, home_dir, strlen(home_dir));
        strncat(token_path, "/", strlen("/"));
        strncat(token_path, TOKEN_FILENAME, sizeof(TOKEN_FILENAME)+1);
    } else {
        /* if token path is too long or $HOME is NULL */
        strncpy(token_path, TOKEN_FILENAME, sizeof(TOKEN_FILENAME));
    }

    FILE *fp = fopen(token_path, "rb");
    if (fp == NULL && (fp = fopen(token_path, "wb")) == NULL) {
        printf("Warning: Failed to create/open the launch token file \"%s\".\n", token_path);
    }
    printf("token_path: %s\n", token_path);
    if (fp != NULL) {
        /* read the token from saved file */
        size_t read_num = fread(token, 1, sizeof(sgx_launch_token_t), fp);
        if (read_num != 0 && read_num != sizeof(sgx_launch_token_t)) {
            /* if token is invalid, clear the buffer */
            memset(&token, 0x0, sizeof(sgx_launch_token_t));
            printf("Warning: Invalid launch token read from \"%s\".\n", token_path);
        }
    }

    /* Step 2: call sgx_create_enclave to initialize an enclave instance */
    /* Debug Support: set 2nd parameter to 1 */

    const void* enclave_ex_p[32] = { 0 };
    enclave_ex_p[SGX_CREATE_ENCLAVE_EX_SWITCHLESS_BIT_IDX] = (void*)us_config;

// Note: DEBUG flag
    //ret = sgx_create_enclave(TESTENCLAVE_FILENAME, SGX_DEBUG_FLAG, &token, &updated, &global_eid, NULL);
    ret = sgx_create_enclave_ex(TESTENCLAVE_FILENAME, SGX_DEBUG_FLAG, &token, &updated, &global_eid, NULL, SGX_CREATE_ENCLAVE_EX_SWITCHLESS, enclave_ex_p);

    if (ret != SGX_SUCCESS) {
        print_error_message(ret);
        if (fp != NULL) fclose(fp);

        return -1;
    }

    /* Step 3: save the launch token if it is updated */

    if (updated == FALSE || fp == NULL) {
        /* if the token is not updated, or file handler is invalid, do not perform saving */
        if (fp != NULL) fclose(fp);
        return 0;
    }

    /* reopen the file with write capablity */
    fp = freopen(token_path, "wb", fp);
    if (fp == NULL) return 0;
    size_t write_num = fwrite(token, 1, sizeof(sgx_launch_token_t), fp);
    if (write_num != sizeof(sgx_launch_token_t))
        printf("Warning: Failed to save launch token to \"%s\".\n", token_path);
    fclose(fp);

    return 0;
}


void switchless_demo(int num)
{
    printf("switchless_demo got called with arg %d\n", num);
    return;
}

sig_atomic_t call_count = 0;

void demo_alrm_handler(int s) {
    call_count++;
}


/* Application entry */
int main(int argc, char *argv[])
{
    (void)(argc);
    (void)(argv);

    /* Changing dir to where the executable is.*/
    char absolutePath[MAX_PATH];
    char *ptr = NULL;

    ptr = realpath(dirname(argv[0]), absolutePath);

    if (ptr == NULL || chdir(absolutePath) != 0)
    	return 1;

    /* Configuration for Switchless SGX */
    sgx_uswitchless_config_t us_config = SGX_USWITCHLESS_CONFIG_INITIALIZER;
    us_config.num_uworkers = 2;
    us_config.num_tworkers = 1;

    /* Demo: register signal handler for SIGALRM */
    printf("Initial value of signal handler-call count: %d\n", call_count);
    struct sigaction alrm_hndler {};
    alrm_hndler.sa_handler = demo_alrm_handler;
    alrm_hndler.sa_flags = 0;
    struct sigaction old_act {};
    sigfillset(&alrm_hndler.sa_mask); // ignore all
    // Note:    HW excpts like SIGBUS will cause errors/stops by urts
    // Note(2): register before enclave create to not overwrite urts handler (but only exists for HW except-signals)
    if (sigaction(SIGALRM, &alrm_hndler, &old_act) < 0) {
        printf("[WARNING] Failed to register handler for SIGALRM\n");
    }
    if (old_act.sa_handler != NULL || old_act.sa_sigaction != NULL) {
        printf("old handler seems to have existed!\n");
    } else {
        printf("no previous handler seems to have existed!\n");
    }

    /* Initialize the enclave */
    if (initialize_enclave(&us_config) < 0)
        return 1; 

    printf("Successfully initialized the Enclave!\n");

    /* Spawn second thread */
    printf("Spawning a second untrusted app thread\n");
    pthread_t second_thread;
    int res = pthread_create(&second_thread, nullptr,
                          run_enclave_test, nullptr);
    if (res != 0) {
        printf("Failed to create a second thread\n");
    }

    // Call second Demo ECALL
    sgx_status_t status = second_thread_demo_call(global_eid);
    if (status != SGX_SUCCESS) {
        printf("Call to second_thread_demo_call has failed.\n");
    }

/*
    // Bench ECALL
    printf("[Enclave] Benching empty ECALL now\n");
    bench_ecall(false);
    bench_ecall(true);

    // Bench OCALL
    printf("[Enclave] Benching empty OCALL now\n");
    call_ocall_bench(false);
    call_ocall_bench(true);
*/

    // Wait for termination of other thread
    printf("Waiting for termination of 2nd thread\n");
    res = pthread_join(second_thread, nullptr);
    if (res != 0) {
        printf("Waiting for termination of 2nd thread failed\n");
    }

    // Now destroy the enclave
    printf("Now destroying the enclave\n");
    res = sgx_destroy_enclave(global_eid);
    if (res != SGX_SUCCESS) {
        printf("Enclave destruction failed\n");
    }
    
    /* signal demo */
    printf("Final value of signal handler-call count: %d\n", call_count);

    return 0;
}

void *run_enclave_test(void *arg) {
    (void)(arg); // UNUSED

    printf("Calling Enclave Demo\n");
    sgx_status_t status = t_sgxssl_call_apis(global_eid);
    if (status != SGX_SUCCESS) {
        printf("Call to t_sgxssl_call_apis has failed.\n");
        printf("Error Status: %d\n", status);
//        return 1;    //Test failed
    }
    printf("Finished Enclave Demo\n");
    return nullptr;
}

int empty_ocall() {
    return 1337;
}

int empty_switchless_ocall() {
    return 1337;
}
