/* Kernel-style GNU C: statement expressions (roadmap 7.4). */
static int kern_sq(int x)
{
	return ({
		int __tmp = x;
		__tmp * __tmp;
	});
}

int kern_use_stmt_expr(int v)
{
	int a = kern_sq(v);
	return a + ({ int b = 2; b + 1; });
}
