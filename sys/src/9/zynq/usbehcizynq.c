#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../port/error.h"
#include	"../port/usb.h"
#include	"usbehci.h"

enum {
	USBMODE = 0x1A8/4,
	USBHOST = 3,
	OTGSC = 0x1A4/4,
	ULPI = 0x170/4,
};

static Ctlr ctlrs[3] = {
	{
		.base = USB0_BASE,
		.irq = USB0IRQ,
	},
	{
		.base = USB1_BASE,
		.irq = USB1IRQ,
	},
};

static void
ehcireset(Ctlr *ctlr)
{
	int i;
	Eopio *opio;

	ilock(ctlr);
	opio = ctlr->opio;
	ehcirun(ctlr, 0);
	opio->cmd |= Chcreset;
	for(i = 0; i < 100; i++){
		if((opio->cmd & Chcreset) == 0)
			break;
		delay(1);
	}
	if(i == 100)
		print("ehci %#p controller reset timed out\n", ctlr->base);
	opio->cmd |= Citc1;
	switch(opio->cmd & Cflsmask){
	case Cfls1024:
		ctlr->nframes = 1024;
		break;
	case Cfls512:
		ctlr->nframes = 512;
		break;
	case Cfls256:
		ctlr->nframes = 256;
		break;
	default:
		panic("ehci: unknown fls %ld", opio->cmd & Cflsmask);
	}
	dprint("ehci: %d frames\n", ctlr->nframes);
	iunlock(ctlr);
}

static int
reset(Hci *hp)
{
	static Lock resetlck;
	Ctlr *ctlr;
	
	ilock(&resetlck);
	for(ctlr = ctlrs; ctlr->base != 0; ctlr++)
		if(!ctlr->active && (hp->port == 0 || hp->port == ctlr->base)){
			ctlr->active = 1;
			break;
		}
	iunlock(&resetlck);
	if(ctlr->base == 0)
		return -1;
	hp->port = ctlr->base;
	hp->irq = ctlr->irq;
	hp->aux = ctlr;
	
	ctlr->r = vmap(ctlr->base, 0x1F0);
	ctlr->opio = (Eopio *) ((uchar *) ctlr->r + 0x140);
	ctlr->capio = (void *) ctlr->base;
	hp->nports = 1;
	ctlr->r[USBMODE] |= USBHOST;
	
	ehcireset(ctlr);
	ehcimeminit(ctlr);
	ehcilinkage(hp);
	ctlr->r[ULPI] = 1<<30 | 1<<29 | 0x0B << 16 | 3<<5;
	if(hp->interrupt != nil)
		intrenable(hp->irq, hp->interrupt, hp, LEVEL, hp->type);
	return 0;
}

void
usbehcilink(void)
{
	ehcidebug = 2;
	addhcitype("ehci", reset);
}