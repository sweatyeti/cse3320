#include <stdio.h>

int main()
{
	FILE * fp = fopen("fat32.img", "r");
	short BPB_BytesPerSec;
	
	fseek(fp, 11, SEEK_SET);
	
	fread(&BPB_BytesPerSec, 1, 2, fp);
	
	printf("Value: %d\n", BPB_BytesPerSec);
}