/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "Team 3",
    /* First member's full name */
    "sjinbk",
    /* First member's email address */
    "sjinbk",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* 기본 상수 정의: word 크기와 기본 heap 확장 단위*/
#define word_size 4
#define chunk_size (1<<12)

/* 추가 상수 정의: header+footer 오버헤드와 split 가능한 최소 block 크기 */
#define block_overhead (2 * word_size)
#define min_block_size (block_overhead + ALIGNMENT)

/* 보조 계산 매크로: 두 값 중 큰 값을 선택 */
#define MAX(x, y)               ( (x) > (y) ? (x) : (y) )

/* 헤더/푸터 패킹 매크로: size와 alloc bit를 한 워드에 함께 패킹 */
#define PACK_INFO(size, alloc)  ( (size) | (alloc) )

/* 메모리 접근 매크로: 워드 단위 읽기/쓰기 */
#define GET_ADDR(p)             ( *(unsigned int *)(p) )
#define PUT_VALUE(p, val)       ( *(unsigned int *)(p) = val )

/* 헤더/푸터 마스킹 매크로: block size와 allocation 여부 추출 */
#define GET_SIZE(p)             ( GET_ADDR(p) & ~0x7)
#define GET_ALLOC(p)            ( GET_ADDR(p) & 0x1 )

/* block 경계 계산 매크로: payload 기준으로 header/footer 위치 계산 */
#define HEADER_BP(bp)           ( (char *)(bp) - word_size )
#define FOOTER_BP(bp)           ( (char *)(bp) - ALIGNMENT/2 + GET_SIZE(HEADER_BP(bp)) - ALIGNMENT/2 )

/* 인접 block 탐색 매크로: 다음/이전 block의 payload 위치 계산 (Pointer Arithmetic) */
#define NEXT_BP(bp)             ( (char *)(bp) + GET_SIZE(((char *)(bp) - word_size)) )
#define PREV_BP(bp)             ( (char *)(bp) - GET_SIZE(((char *)(bp) - ALIGNMENT)) )

/* 전역 heap 시작 포인터: prologue의 payload를 가리키며 implicit list 순회의 기준점 */
static char *heap_pointer = NULL;

/* 내부 helper 선언 */
static void *extend_heap(size_t words);     // heap 확장
static void *coalesce(void *bp);            // Coalescing
static void *find_fit(size_t adjusted_size);        // Placement (First-Fit)
static void place(void *bp, size_t adjusted_size);  // Splitting
static size_t adjust_block_size(size_t size);       // 요청 payload를 실제 block 크기로 보정

/* heap 확장 */
// 입력
    // words: heap에 추가할 word 단위 크기

// 출력
    // 새 free block의 payload 시작 주소
    // 확장 실패 시, NULL 리턴
static void *extend_heap(size_t words) {

    char *bp;       // 새로 확장된 free block의 payload 시작 주소
    size_t size;    // alignment 조건을 만족하도록 보정한 실제 확장 바이트 수

    // 동작 명세: 새 free block과 epillogue를 생성 후, 인접 free block과 병합
        // STEP 1. block alignment 유지를 위해, 확장 크기를 짝수 word 개수로 맞춤.
        size = (words % 2) ? (words + 1) * word_size : words * word_size;

        // STEP 2. 계산된 heap의 끝을 size 만큼 늘리도록 system call 요청 후 시작 주소 받음.
        bp = mem_sbrk(size);

        // STEP 3. system call 실패 시, NULL 리턴
        if ( (long)bp == -1 ) {
            return NULL;
        }
        
        // STEP 4. 확장된 heap을 하나의 free block으로 보고 header, footer 기록.
        PUT_VALUE( HEADER_BP(bp), PACK_INFO(size, 0) );
        PUT_VALUE( FOOTER_BP(bp), PACK_INFO(size, 0) );

        // STEP 5. free block 바로 뒤에 epilogue block 기록.
        PUT_VALUE( HEADER_BP(NEXT_BP(bp)), PACK_INFO(0, 1) );

        // STEP 6. 이전 block이 free였을 수 있으므로 즉시 coalescing을 수행. 
        // ... [free block][new free block][epilogue]
        return coalesce(bp);
}

/* Coalescing */
// 입력
    // bp: free 상태로 표시된 block의 payload 시작 주소

// 출력
    // 병합된 block의 payload 시작 주소
static void *coalesce(void *bp) {

    size_t prev_alloc = GET_ALLOC(FOOTER_BP(PREV_BP(bp)));  // 이전 block의 allocation bit
    size_t next_alloc = GET_ALLOC(HEADER_BP(NEXT_BP(bp)));  // 다음 block의 allocation bit
    size_t size = GET_SIZE(HEADER_BP(bp));                  // 현재 block을 기준으로 병합 후 갱신될 최종 크기



    // 동작 명세: 이전/다음 block의 상태를 보고 4가지 경우의 수에 대한 병합 처리

        // Case 1: 이전 block 할당됨, 다음 block 할당됨
        // [prev: alloc] [current: free] [next: alloc]
        if (prev_alloc && next_alloc) {
            // 합칠 대상이 없어. bp 그대로 반환.
            return bp;
        }

        // Case 2: 이전 block 할당됨, 다음 block free
        // [prev: alloc] [current: free] [next: free]
        else if (prev_alloc && !next_alloc) {
            // 2-1. 다음 block의 크기를 size에 더해
            size += GET_SIZE(HEADER_BP(NEXT_BP(bp)));
            // 2-2. 현재 block의 header와 다음 block의 footer를 새 size로 업데이트
            PUT_VALUE(HEADER_BP(bp), PACK_INFO(size, 0));
            PUT_VALUE(FOOTER_BP(bp), PACK_INFO(size, 0));   // 합쳐진 block 크기를 기준으로 footer 위치를 계산
        }

        // Case 3: 이전 block free, 다음 block 할당됨
        // [prev: free] [current: free] [next: alloc]
        else if (!prev_alloc && next_alloc) {
            // 3-1. 이전 block의 크기를 size에 더해
            size += GET_SIZE(HEADER_BP(PREV_BP(bp)));
            // 3-2. 이전 block의 header와 현재 block의 footer를 새 size로 업데이트
            PUT_VALUE(HEADER_BP(PREV_BP(bp)), PACK_INFO(size, 0));
            PUT_VALUE(FOOTER_BP(bp), PACK_INFO(size, 0));
            // 3-3. bp를 이전 block으로 이동 (합쳐진 block의 시작점이 이전 block이니까)
            bp = PREV_BP(bp);
        }

        // Case 4: 이전 block free, 다음 block free
        // [prev: free] [current: free] [next: free]
        else {
            // 4-1. 이전 block과 다음 block의 크기를 size에 더해
            size += GET_SIZE(HEADER_BP(PREV_BP(bp))) + GET_SIZE(FOOTER_BP(NEXT_BP(bp)));
            // 4-2. 이전 block의 header와 다음 block의 footer를 새 size로 업데이트
            PUT_VALUE(HEADER_BP(PREV_BP(bp)), PACK_INFO(size, 0));
            PUT_VALUE(FOOTER_BP(NEXT_BP(bp)), PACK_INFO(size, 0));
            // 4-3. bp를 이전 block으로 이동 (합쳐진 block의 시작점이 이전 block이니까)
            bp = PREV_BP(bp);
        }

        return bp;
}

/* Placement (First-Fit) */
// 입력
    // adjusted_size: 정렬과 메타데이터를 반영한 실제 필요 block 크기

// 출력
    // first-fit 정책으로 찾은 free block의 payload 시작 주소
    // 적합한 block이 없으면 NULL
static void *find_fit(size_t adjusted_size)
{
    void *bp;   // implicit free list를 순회할 현재 block 포인터

    // 동작 명세: heap_pointer부터 epilogue 직전까지 선형 탐색
    for (bp = heap_pointer; GET_SIZE(HEADER_BP(bp)) > 0; bp = NEXT_BP(bp)) {
        // free 상태이면서 요청 크기 이상인 첫 block pointer를 즉시 반환
        if ( 
            !GET_ALLOC(HEADER_BP(bp)) && 
            (adjusted_size <= GET_SIZE(HEADER_BP(bp)))
        )
            return bp;
    }
        // 적합한 block이 없으면 NULL
        return NULL;
}

/* Splitting */
// 입력
    // bp: 배치 대상으로 선택된 free block의 payload 시작 주소
    // adjusted_size: 실제로 할당해야 하는 block 크기

// 출력
    // 없음(void)
static void place(void *bp, size_t adjusted_size)
{
    size_t current_size = GET_SIZE(HEADER_BP(bp));  // 선택된 free block의 현재 전체 크기

    // 동작 명세: block을 할당 처리하고 남는 공간이 충분하면 분할
        // STEP 1. 나머지 공간이 최소 block 크기(헤더+푸터+정렬 단위) 이상이면 split
        if ((current_size - adjusted_size) >= min_block_size) {
            // STEP 1-1 .앞부분은 요청 크기의 allocated block으로 표시
            PUT_VALUE(HEADER_BP(bp), PACK_INFO(adjusted_size, 1));
            PUT_VALUE(FOOTER_BP(bp), PACK_INFO(adjusted_size, 1));
            // STEP 1-2. 남은 뒷부분으로 이동해 새 free block의 header/footer를 기록
            bp = NEXT_BP(bp);
            PUT_VALUE(HEADER_BP(bp), PACK_INFO(current_size - adjusted_size, 0));
            PUT_VALUE(FOOTER_BP(bp), PACK_INFO(current_size - adjusted_size, 0));
        }
        // STEP 2. 나머지 공간 부족하면, 분할하지 않고 block 전체를 할당한다.
        else {
            PUT_VALUE(HEADER_BP(bp), PACK_INFO(current_size, 1));
            PUT_VALUE(FOOTER_BP(bp), PACK_INFO(current_size, 1));
        }
}

/* block size 보정 */
// 입력
    // size: 사용자가 요청한 payload 바이트 수

// 출력
    // header/footer와 alignment를 반영한 실제 block 크기
static size_t adjust_block_size(size_t size)
{
    // 동작 명세: 작은 요청은 최소 block 크기로 올리고, 일반 요청은 정렬 단위에 맞춰 반올림

    if (size <= ALIGNMENT) {
        // STEP 1. payload가 alignment 이하이면 최소 allocated block 크기 반환
        return 2 * ALIGNMENT;
    }

    // STEP 2. 아니면 block overhead를 더한 뒤 alignment 배수로 올림
    return ((size + block_overhead + (ALIGNMENT - 1)) / ALIGNMENT) * ALIGNMENT;
}

/* mm_init */
// 입력
    // 인자 없음(void)

// 출력
    // -1: 초기화 불가 / 0: 정상 초기화
int mm_init(void)
{

    // 동작 명세 : prologue/epilogue가 포함된 초기 heap 구조를 만들고 첫 free block을 생성
        // STEP 1. 시스템 콜하여 초기 공간 block 4개 요청(sbrk)
        if ( (heap_pointer = mem_sbrk(4 * word_size)) == (void *)-1 ) {
            return -1;
        }
        // STEP 2. heap 구조 세팅
        PUT_VALUE(heap_pointer, 0);                                         // alignment padding
        PUT_VALUE(heap_pointer + (1 * word_size), PACK_INFO(ALIGNMENT, 1));  // prologue header
        PUT_VALUE(heap_pointer + (2 * word_size), PACK_INFO(ALIGNMENT, 1));  // prologue footer
        PUT_VALUE(heap_pointer + (3 * word_size), PACK_INFO(0, 1));          // epilogude header
        
        // STEP 3. 순회 시작점을 prologue의 payload 위치(header, footer 사이)로 옮긴다
        heap_pointer += (2 * word_size);

        // STEP 4. 초기 free block을 만들기 위해 기본 chunk 크기만큼 heap을 확장
        if (extend_heap(chunk_size/word_size) == NULL) {
            return -1;
        }
        
        // STEP 5. 초기 설정이 모두 끝났으므로 성공 반환
        return 0;
}

/* mm_malloc */
void *mm_malloc(size_t size)
{
    size_t adjusted_size;   // header/footer와 정렬을 포함한 실제 할당 크기
    size_t extend_size;     // 적합한 free block이 없을 때 추가로 확장할 크기
    char *bp;               // 최종적으로 배치할 block의 payload 시작 주소

    // 입력
        // size: 사용자가 요청한 payload 바이트 수

    // 출력
        // 할당된 block의 payload 시작 주소
        // 요청이 0이거나 할당 실패 시 NULL 리턴

    // 동작 명세: 요청 크기를 보정하고, First-Fit 탐색 후, 필요 시 heap 을 확장

        // STEP 1. byte 요청은 의미 있는 block을 만들지 않으므로 바로 NULL을 반환
        if (size == 0)
            return NULL;

        if (size <= ALIGNMENT)
            // STEP 2. double-alignment 보다 작은 요청도 최소 block 크기를 만족하도록 조정
            adjusted_size = 2 * ALIGNMENT;
        else
            // STEP 3. 일반 요청은 size + header/footer 값에 대하여 alignment 기준 올림 계산
            adjusted_size = ((size + (2 * word_size) + (ALIGNMENT - 1)) / ALIGNMENT) * ALIGNMENT;

        // STEP 4. 현재 implicit free list에서 First-Fit block을 찾는다.
        adjusted_size = adjust_block_size(size);

        if ((bp = find_fit(adjusted_size)) != NULL) {
            // STEP 5. 적합한 block을 찾았으면 필요 시 split해서 배치하고 주소를 반환
            place(bp, adjusted_size);
            return bp;
        }

        // STEP 6. 적합한 block이 없으면 요청 크기와 기본 chunk 중 더 큰 값만큼 heap을 확장
        extend_size = MAX(adjusted_size, chunk_size);
        if ((bp = extend_heap(extend_size/word_size)) == NULL)
            return NULL;
        // STEP 7. 새로 확보한 free block에 요청 block을 배치
        place(bp, adjusted_size);
        return bp;
}

/* mm_free */
// 입력
    // ptr: 해제할 block의 payload 시작 주소

// 출력
    // 없음(void)
void mm_free(void *ptr)
{
    size_t size = GET_SIZE(HEADER_BP(ptr)); // 해제 대상 block의 전체 크기

    // 동작 명세: 현재 block을 free 상태로 바꾸고 즉시 병합
        // STEP 1. header/footer의 alloc bit를 0으로 바꿔 free block임을 표시
        PUT_VALUE(HEADER_BP(ptr), PACK_INFO(size, 0));
        PUT_VALUE(FOOTER_BP(ptr), PACK_INFO(size, 0));
        // STEP 2. 인접한 free block이 있으면 하나의 큰 free block으로 병합
        coalesce(ptr);
}

/* mm_realloc */
// 입력
    // ptr  : 기존에 할당된 block의 payload 시작 주소
    // size : 새로 요청된 payload 바이트 수

// 출력
    // 재할당 결과 block의 payload 시작 주소

// 전체 동작 명세
    /* 
    ┌─ [예외 처리]
    │   ├─ ptr == NULL  → mm_malloc(size)
    │   └─ size == 0    → mm_free(ptr), return NULL
    │
    ├─ 요청 크기 보정
    │   ├─ adjusted_size = adjust_block_size(size)
    │   └─ old_size = 현재 block 전체 크기
    │
    ├─ [경우 1] 이미 현재 block이 충분히 큰가?
    │   ├─ YES
    │   │   ├─ 남는 공간이 min_block_size 이상?
    │   │   │   ├─ YES → 뒤를 split해서 free block 생성
    │   │   │   └─ NO  → 그냥 그대로 사용
    │   │   └─ return oldptr
    │   └─ NO
    │
    ├─ 인접 block 조사
    │   ├─ prev_bp, next_bp
    │   ├─ prev_size, next_size
    │   └─ next가 epilogue면 heap 확장 시도
    │
    ├─ [경우 2] 뒤 block만 합쳐서 해결 가능한가?
    │   ├─ next free && old + next >= adjusted_size
    │   │   ├─ 현재 위치에서 뒤로 확장
    │   │   ├─ 필요하면 split
    │   │   └─ return oldptr
    │   └─ 실패
    │
    ├─ [경우 3] 앞 + 현재 + 뒤 세 block 합치면 가능한가?
    │   ├─ prev free && next free && prev + old + next >= adjusted_size
    │   │   ├─ 시작점이 prev_bp로 이동
    │   │   ├─ payload memmove
    │   │   ├─ 필요하면 split
    │   │   └─ return prev_bp
    │   └─ 실패
    │
    ├─ [경우 4] 앞 block만 합치면 가능한가?
    │   ├─ prev free && prev + old >= adjusted_size
    │   │   ├─ 시작점이 prev_bp로 이동
    │   │   ├─ payload memmove
    │   │   ├─ 필요하면 split
    │   │   └─ return prev_bp
    │   └─ 실패
    │
    └─ [최후 수단]
        ├─ newptr = mm_malloc(size)
        ├─ payload 복사
        ├─ mm_free(oldptr)
        └─ return newptr
    */

void *mm_realloc(void *ptr, size_t size) {
    void *oldptr = ptr;      // 기존 block의 payload 시작 주소
    void *newptr;            // 제자리/인접 확장 실패 시 새로 할당할 block의 payload 시작 주소
    void *prev_bp;           // 이전 block의 payload 시작 주소
    void *next_bp;           // 다음 block의 payload 시작 주소
    size_t adjusted_size;    // 요청 payload를 alignment + overhead까지 반영한 실제 block 크기
    size_t old_size;         // 기존 block의 전체 크기(header/footer 포함)
    size_t combined_size;    // 인접 block과 합쳤을 때의 전체 크기
    size_t copySize;         // 실제로 복사할 payload 바이트 수
    size_t prev_size;        // 이전 block의 전체 크기
    size_t next_size;        // 다음 block의 전체 크기

    /* 1. 예외 처리 */

    // realloc(NULL, size)는 malloc(size)와 같은 의미
    if (oldptr == NULL) {
        return mm_malloc(size);
    }

    // realloc(ptr, 0)은 기존 block을 해제하고 NULL 반환
    if (size == 0) {
        mm_free(oldptr);
        return NULL;
    }

    /* 2. 요청 크기와 현재 block 크기 계산 */

    // 요청 payload 크기를 실제 block 크기 단위로 보정
    adjusted_size = adjust_block_size(size);

    // 현재 block의 전체 크기를 읽음
    old_size = GET_SIZE(HEADER_BP(oldptr));

    /* 3. 현재 block만으로 해결 가능한 경우 */

    // 이미 현재 block이 충분히 크면 기존 위치를 그대로 재사용
    if (old_size >= adjusted_size) {
        // 남는 공간이 최소 free block 크기 이상이면 뒤쪽을 분리해서 free block으로 남김
        if ((old_size - adjusted_size) >= min_block_size) {
            place(oldptr, adjusted_size);

            // split으로 생긴 뒤 free block이 다음 free block과 이어질 수 있으므로 병합
            coalesce(NEXT_BP(oldptr));
        }

        // payload 시작 주소가 바뀌지 않으므로 기존 포인터 반환
        return oldptr;
    }

    /* 4. 인접 block 정보 확인 */

    // 현재 block만으로는 부족하므로 앞/뒤 인접 block 정보를 읽음
    prev_bp = PREV_BP(oldptr);
    next_bp = NEXT_BP(oldptr);
    prev_size = GET_SIZE(HEADER_BP(prev_bp));
    next_size = GET_SIZE(HEADER_BP(next_bp));

    // 다음 block이 epilogue라면 heap 끝에 도달한 상태이므로 heap 확장 시도
    if (next_size == 0) {
        // 부족한 크기와 기본 확장 단위 중 더 큰 값만큼 heap 확장
        size_t extend_size = MAX(adjusted_size - old_size, chunk_size);
        if (extend_heap(extend_size / word_size) != NULL) {
            // heap 확장 후 oldptr 뒤쪽에 새 free block이 생기므로 정보 갱신
            next_bp = NEXT_BP(oldptr);
            next_size = GET_SIZE(HEADER_BP(next_bp));
        }
    }

    /* 5. 뒤 free block과 합쳐 제자리 확장 가능한지 확인 */

    // 다음 block이 free라면 현재 block과 합쳐서 payload 시작 주소를 유지한 채 확장 시도
    if (!GET_ALLOC(HEADER_BP(next_bp))) {
        combined_size = old_size + next_size;

        // 현재 + 뒤 block만으로 요청 크기를 만족할 수 있으면 제자리 확장 가능
        if (combined_size >= adjusted_size) {
            // 현재 block부터 다음 block까지를 하나의 큰 allocated block으로 먼저 묶음
            PUT_VALUE(HEADER_BP(oldptr), PACK_INFO(combined_size, 1));
            PUT_VALUE(FOOTER_BP(next_bp), PACK_INFO(combined_size, 1));

            // 요청 크기 기준으로 다시 배치하여 남는 공간이 있으면 뒤쪽을 split
            place(oldptr, adjusted_size);

            // split 결과 남은 free block이 있다면 다음 free block과 병합
            if ((combined_size - adjusted_size) >= min_block_size) {
                coalesce(NEXT_BP(oldptr));
            }

            // payload 시작 주소가 바뀌지 않았으므로 기존 포인터 반환
            return oldptr;
        }
    }

    /* 6. 앞+현재+뒤를 합쳐 확장 가능한지 확인 */

    // 앞/뒤 block이 모두 free이면 세 block을 합쳐 더 큰 공간을 확보할 수 있음
    // 이 경우 새 payload 시작 주소는 prev_bp가 되므로 데이터 이동이 필요
    if (!GET_ALLOC(HEADER_BP(prev_bp)) && !GET_ALLOC(HEADER_BP(next_bp))) {
        combined_size = old_size + prev_size + next_size;

        if (combined_size >= adjusted_size) {
            // 복사 대상은 기존 payload뿐이므로 overhead(header/footer)는 제외
            copySize = old_size - block_overhead;

            // 새 요청 payload 크기를 넘겨 복사하지 않도록 상한을 size로 제한
            if (size < copySize) {
                copySize = size;
            }

            // prev block부터 next block까지를 하나의 큰 allocated block으로 묶음
            PUT_VALUE(HEADER_BP(prev_bp), PACK_INFO(combined_size, 1));
            PUT_VALUE(FOOTER_BP(next_bp), PACK_INFO(combined_size, 1));

            // 시작 주소가 prev_bp로 바뀌므로 기존 payload를 앞으로 이동
            memmove(prev_bp, oldptr, copySize);

            // 요청 크기에 맞게 다시 배치하고 남는 공간은 뒤쪽 free block으로 분리
            place(prev_bp, adjusted_size);

            // split 결과 생긴 free block이 다음 free block과 이어질 수 있으므로 병합
            if ((combined_size - adjusted_size) >= min_block_size) {
                coalesce(NEXT_BP(prev_bp));
            }

            // 새 payload 시작 주소 반환
            return prev_bp;
        }
    }

    /* 7. 앞 free block과만 합쳐 확장 가능한지 확인 */

    // 앞 block만 free라면 시작 주소를 앞으로 옮기는 방식으로 확장 시도
    if (!GET_ALLOC(HEADER_BP(prev_bp))) {
        combined_size = old_size + prev_size;

        if (combined_size >= adjusted_size) {
            // 복사 대상은 기존 payload이며, 새 요청 payload 크기를 넘기지 않도록 제한
            copySize = old_size - block_overhead;
            if (size < copySize) {
                copySize = size;
            }

            // prev block부터 old block까지를 하나의 큰 allocated block으로 묶음
            PUT_VALUE(HEADER_BP(prev_bp), PACK_INFO(combined_size, 1));
            PUT_VALUE(FOOTER_BP(oldptr), PACK_INFO(combined_size, 1));

            // 새 payload 시작 주소가 prev_bp로 바뀌므로 데이터를 앞으로 이동
            memmove(prev_bp, oldptr, copySize);

            // 요청 크기 기준으로 다시 배치하고 남는 공간은 뒤쪽 free block으로 분리
            place(prev_bp, adjusted_size);

            // split 결과 생긴 free block이 다음 free block과 이어질 수 있으므로 병합
            if ((combined_size - adjusted_size) >= min_block_size) {
                coalesce(NEXT_BP(prev_bp));
            }

            // 새 payload 시작 주소 반환
            return prev_bp;
        }
    }

    /* 8. 최후 수단: 새 block 할당 후 복사 */

    // 제자리 재사용과 인접 block 확장이 모두 실패하면 새 block을 할당
    newptr = mm_malloc(size);
    if (newptr == NULL) {
        return NULL;
    }

    // 기존 block 전체가 아니라 payload만 복사해야 하므로 overhead는 제외
    copySize = old_size - block_overhead;

    // 새 요청 payload 크기를 넘겨 복사하지 않도록 상한을 size로 제한
    if (size < copySize) {
        copySize = size;
    }

    // 새 block으로 기존 payload를 복사
    memcpy(newptr, oldptr, copySize);

    // 기존 block은 더 이상 필요 없으므로 해제
    mm_free(oldptr);

    // 새로 확보한 block의 payload 시작 주소 반환
    return newptr;
}