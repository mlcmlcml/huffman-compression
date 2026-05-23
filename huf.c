#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Node {
    long freq;
    int byte;
    struct Node *left, *right;
} Node;

Node *new_node(int byte, long freq) {
    Node *n = malloc(sizeof(Node));
    n->byte = byte; n->freq = freq;
    n->left = n->right = NULL;
    return n;
}

#define MAX_NODES 512
Node *heap[MAX_NODES];
int heap_size = 0;

void heap_push(Node *n) {
    heap[heap_size++] = n;
    int i = heap_size - 1;
    while (i > 0) {
        int p = (i-1)/2;
        if (heap[p]->freq > heap[i]->freq) {
            Node *tmp = heap[p]; heap[p] = heap[i]; heap[i] = tmp; i = p;
        } else break;
    }
}

Node *heap_pop() {
    Node *top = heap[0];
    heap[0] = heap[--heap_size];
    int i = 0;
    while (1) {
        int l=2*i+1, r=2*i+2, s=i;
        if (l < heap_size && heap[l]->freq < heap[s]->freq) s = l;
        if (r < heap_size && heap[r]->freq < heap[s]->freq) s = r;
        if (s == i) break;
        Node *tmp = heap[i]; heap[i] = heap[s]; heap[s] = tmp; i = s;
    }
    return top;
}

Node *build_huffman_tree(long freq[256]) {
    heap_size = 0;
    for (int i = 0; i < 256; i++)
        if (freq[i] > 0) heap_push(new_node(i, freq[i]));
    if (heap_size == 1) {
        Node *only = heap_pop();
        Node *root = new_node(-1, only->freq);
        root->left = only;
        return root;
    }
    while (heap_size > 1) {
        Node *a = heap_pop(), *b = heap_pop();
        Node *p = new_node(-1, a->freq + b->freq);
        p->left = a; p->right = b;
        heap_push(p);
    }
    return heap_pop();
}

char codes[256][257];

void generate_codes(Node *n, char *path, int depth) {
    if (n == NULL) return;
    if (n->byte >= 0) {
        path[depth] = '\0';
        if (depth == 0) { path[0]='0'; path[1]='\0'; }
        strcpy(codes[n->byte], path);
        return;
    }
    path[depth] = '0'; generate_codes(n->left,  path, depth+1);
    path[depth] = '1'; generate_codes(n->right, path, depth+1);
}

// 压缩单个文件，写入fout，同时返回压缩数据占用的字节数
void compress_one(const char *input_file, FILE *fout) {
    long freq[256] = {0};
    FILE *fin = fopen(input_file, "rb");
    if (!fin) { printf("错误：无法打开 %s\n", input_file); return; }
    int byte;
    while ((byte = fgetc(fin)) != EOF) freq[byte]++;
    fclose(fin);

    Node *root = build_huffman_tree(freq);
    memset(codes, 0, sizeof(codes));
    char path[257];
    generate_codes(root, path, 0);

    long total_bytes = 0;
    for (int i = 0; i < 256; i++) total_bytes += freq[i];

    // 计算压缩数据字节数，提前写入方便解压时跳过
    long total_bits = 0;
    for (int i = 0; i < 256; i++)
        if (freq[i] > 0) total_bits += freq[i] * strlen(codes[i]);
    long compressed_bytes = (total_bits + 7) / 8;

    // 写文件名
    int name_len = strlen(input_file);
    fwrite(&name_len, sizeof(int), 1, fout);
    fwrite(input_file, 1, name_len, fout);
    // 写原始字节数
    fwrite(&total_bytes, sizeof(long), 1, fout);
    // 写压缩数据字节数
    fwrite(&compressed_bytes, sizeof(long), 1, fout);
    // 写频率表
    fwrite(freq, sizeof(long), 256, fout);

    // 写压缩数据
    fin = fopen(input_file, "rb");
    unsigned char buf = 0;
    int bit_count = 0;
    while ((byte = fgetc(fin)) != EOF) {
        char *code = codes[byte];
        for (int i = 0; code[i] != '\0'; i++) {
            buf = (buf << 1) | (code[i] - '0');
            bit_count++;
            if (bit_count == 8) {
                fwrite(&buf, 1, 1, fout);
                buf = 0; bit_count = 0;
            }
        }
    }
    if (bit_count > 0) {
        buf = buf << (8 - bit_count);
        fwrite(&buf, 1, 1, fout);
    }
    fclose(fin);
    printf("  已压缩：%s（%ld 字节）\n", input_file, total_bytes);
}

void decompress_one(FILE *fin) {
    // 读文件名
    int name_len;
    if (fread(&name_len, sizeof(int), 1, fin) != 1) return;
    char filename[256];
    fread(filename, 1, name_len, fin);
    filename[name_len] = '\0';

    // 读原始字节数和压缩数据字节数
    long total_bytes, compressed_bytes;
    fread(&total_bytes,    sizeof(long), 1, fin);
    fread(&compressed_bytes, sizeof(long), 1, fin);

    // 读频率表，重建树
    long freq[256];
    fread(freq, sizeof(long), 256, fin);
    Node *root = build_huffman_tree(freq);

    // 读压缩数据到内存buffer，精确读取compressed_bytes个字节
    unsigned char *cbuf = malloc(compressed_bytes);
    fread(cbuf, 1, compressed_bytes, fin);

    // 解压写出
    FILE *fout = fopen(filename, "wb");
    if (!fout) { printf("错误：无法创建 %s\n", filename); free(cbuf); return; }

    Node *cur = root;
    long decoded = 0;
    for (long bi = 0; bi < compressed_bytes && decoded < total_bytes; bi++) {
        for (int i = 7; i >= 0 && decoded < total_bytes; i--) {
            int bit = (cbuf[bi] >> i) & 1;
            cur = (bit == 0) ? cur->left : cur->right;
            if (cur->byte >= 0) {
                fputc(cur->byte, fout);
                decoded++;
                cur = root;
            }
        }
    }
    fclose(fout);
    free(cbuf);
    printf("  已解压：%s（%ld 字节）\n", filename, decoded);
}

void compress_multi(int file_count, char *files[], const char *output_file) {
    FILE *fout = fopen(output_file, "wb");
    if (!fout) { printf("错误：无法创建 %s\n", output_file); return; }
    fwrite(&file_count, sizeof(int), 1, fout);
    for (int i = 0; i < file_count; i++)
        compress_one(files[i], fout);
    fclose(fout);
    printf("压缩完成！-> %s\n", output_file);
}

void decompress_multi(const char *input_file) {
    FILE *fin = fopen(input_file, "rb");
    if (!fin) { printf("错误：无法打开 %s\n", input_file); return; }
    int file_count;
    fread(&file_count, sizeof(int), 1, fin);
    printf("压缩包内共 %d 个文件：\n", file_count);
    for (int i = 0; i < file_count; i++)
        decompress_one(fin);
    fclose(fin);
    printf("解压完成！\n");
}

void print_usage(const char *prog) {
    printf("用法：\n");
    printf("  压缩：%s -c <压缩包.huf> <文件1> [文件2] ...\n", prog);
    printf("  解压：%s -d <压缩包.huf>\n", prog);
}

int main(int argc, char *argv[]) {
    printf("=== Huffman 文件压缩工具 ===\n\n");
    if (argc < 3) { print_usage(argv[0]); return 1; }

    char *mode     = argv[1];
    char *huf_file = argv[2];

    if (strcmp(mode, "-c") == 0) {
        if (argc < 4) { printf("错误：请指定至少一个输入文件\n"); return 1; }
        int file_count = argc - 3;
        printf("正在压缩 %d 个文件 -> %s\n", file_count, huf_file);
        compress_multi(file_count, &argv[3], huf_file);
    } else if (strcmp(mode, "-d") == 0) {
        printf("正在解压 %s\n\n", huf_file);
        decompress_multi(huf_file);
    } else {
        printf("错误：未知参数 %s\n\n", mode);
        print_usage(argv[0]);
        return 1;
    }
    return 0;
}