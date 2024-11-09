all :
	gcc -m32 -nostdlib -no-pie -o sum sum.c
	gcc -m32 -nostdlib -no-pie -o fib fib.c
	gcc -m32 -nostdlib -no-pie -o prime prime.c
	gcc -m32 -o loader smloader.c
clean:
	rm -f sum fib loader prime