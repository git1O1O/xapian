/* da_database.cc: C++ class for datype access routines */

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <string>

#include "database.h"
#include "da_database.h"
#include "daread.h"

DAPostList::DAPostList(struct postings *pl, doccount tf, doccount size)
{
    termfreq = tf;
    postlist = pl;

    termweight = (size - tf + 0.5) / (tf + 0.5);
    if(termweight < 1) termweight = 1;

    termweight = log(termweight);

    printf("(dbsize, termfreq) = (%4d, %4d)\t=> termweight = %f\n",
	   size, tf, termweight);

    DAreadpostings(postlist, 0, 0);
}

DAPostList::~DAPostList()
{
    DAclosepostings(postlist);
}

/* This is the biggie */
weight DAPostList::get_weight() const
{
    if(at_end()) throw OmError("Attempt to access beyond end of postlist.");
    doccount wdf;
    weight wt;

    wdf = postlist->wdf;

//    printf("(wdf, termweight)  = (%4d, %4.2f)", wdf, termweight);

    const double k = 1;
    // FIXME - precalculate this freq score for several values of wt - may
    // remove much computation.
    wt = (double) wdf / (k + wdf);
//    printf("(freq score %4.2f)", wt);

    wt *= termweight;

//    printf("\t=> weight = %f\n", wt);

    return wt;
}

void DAPostList::next()
{
    if(at_end()) throw OmError("Attempt to access beyond end of postlist.");
    DAreadpostings(postlist, 0, 0);
}

void DAPostList::skip_to(docid id)
{
    if(at_end()) throw OmError("Attempt to access beyond end of postlist.");
    DAreadpostings(postlist, 0, id);
}



DATermList::DATermList(DADatabase *db, struct termvec *tv)
{
    tvec = tv;
    dbase = db;

    readterms(tvec);
}

DATermList::~DATermList()
{
    losetermvec(tvec);
}

termid DATermList::get_termid()
{
    if(at_end()) throw OmError("Attempt to access beyond end of termlist.");

    char *term = (char *)tvec->term;

    return dbase->term_name_to_id(string(term + 1, (unsigned)term[0]));
}

void   DATermList::next()
{
    readterms(tvec);
}

bool   DATermList::at_end()
{
    if(tvec->term == 0) return true;
    return false;
}



DATerm::DATerm(struct terminfo *ti_new, termname name_new)
{
    ti = ti_new;
    name = name_new;
}

DATerm::~DATerm()
{
    ;
}



DADatabase::DADatabase()
{
    DA_r = NULL;
    DA_t = NULL;
    max_termid = 0;
    opened = false;
}

void
DADatabase::open(string pathname, bool readonly)
{
    DADatabase::close();

    string filename_r = pathname + "/R";
    string filename_t = pathname + "/T";

    DA_r = DAopen((byte *)(filename_r.c_str()), DARECS);
    if(DA_r == NULL)
	throw OpeningError(string("When opening ") + filename_r + ": " + strerror(errno));

    DA_t = DAopen((byte *)(filename_t.c_str()), DATERMS);
    if(DA_t == NULL) {
	DAclose(DA_r);
	DA_r = NULL;
	throw OpeningError(string("When opening ") + filename_t + ": " + strerror(errno));
    }

    dbsize = DA_r->itemcount;

    opened = true;

    return;
}

void
DADatabase::close()
{
    if(DA_r != NULL) {
	DAclose(DA_r);
	DA_r = NULL;
    }
    if(DA_t != NULL) {
	DAclose(DA_t);
	DA_t = NULL;
    }
    opened = false;
}

PostList * DADatabase::open_post_list(termid id)
{
    if(!opened) throw OmError("DADatabase not opened.");

    struct postings * postlist;
    //Assert(id > 0 && id <= termvec.size())
    postlist = DAopenpostings(termvec[id - 1].ti, DA_t);

    DAPostList * pl = new DAPostList(postlist, termvec[id - 1].ti->freq, dbsize);
    return pl;
}

TermList * DADatabase::open_term_list(docid id)
{
    if(!opened) throw OmError("DADatabase not opened.");

    struct termvec *tv = maketermvec();
    int found = DAgettermvec(DA_r, id, tv);

    if(found == 0) throw RangeError("Docid not found");

    openterms(tv);

    DATermList *tl = new DATermList(this, tv);
    return tl;
}

termid
DADatabase::term_name_to_id(termname name)
{
    printf("Looking up term `%s': ", name.c_str());
    if(!opened) throw OmError("DADatabase not opened.");

    map<termname,termid>::iterator p = termidmap.find(name);

    termid id = 0;
    if (p == termidmap.end()) {
	int len = name.length();
	if(len > 126) throw OmError("Term too long for DA implementation.");
	byte * k = (byte *) malloc(len + 1);
	if(k == NULL) throw OmError(strerror(ENOMEM));
	k[0] = len + 1;
	name.copy((char*)(k + 1), len);

	struct terminfo *ti = new struct terminfo;
	int found = DAterm(k, ti, DA_t);
	free(k);

	if(found == 0) {
	    delete ti;
	} else {
	    id = termvec.size() + 1;
	    printf("Adding as ID %d\n", id);
	    termvec.push_back(DATerm(ti, name));
	    termidmap[name] = id;
	}
    } else {
	id = (*p).second;
	printf("found, ID %d\n", id);
    }
    return id;
}

termname
DADatabase::term_id_to_name(termid id)
{
    if(!opened) throw OmError("DADatabase not opened.");
    if (id <= 0 || id > termvec.size()) throw RangeError("invalid termid");
    printf("Looking up termid %d: name = `%s'\n", id, termvec[id - 1].name.c_str());
    return termvec[id - 1].name;
}
