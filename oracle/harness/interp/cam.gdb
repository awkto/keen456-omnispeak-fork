set pagination off
set confirm off
handle SIGPIPE nostop noprint
dprintf RF_Refresh,"CAM %d %d %d\n", rf_scrollXUnit, rf_scrollYUnit, ck_keenObj ? ck_keenObj->posX : -1
run
quit
