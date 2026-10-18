/* Kernel-style GNU C: computed goto (roadmap 7.12). */
int kern_use_computed_goto(int op)
{
	static void *const table[] = { &&kern_op_nop, &&kern_op_inc, &&kern_op_dec };
	int result = 0;

	if (op < 0 || op > 2)
		goto kern_op_default;
	goto *table[op];

kern_op_nop:
	return result;
kern_op_inc:
	result++;
	return result;
kern_op_dec:
	result--;
	return result;
kern_op_default:
	return -1;
}
