/* percent.c - one public function, one private helper. */

/* 'static': only this file can call clamp. */
static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* No 'static': other files may call percent. */
int percent(int part, int whole)
{
    if (whole <= 0)
        return 0;
    return clamp(part * 100 / whole, 0, 100);
}
