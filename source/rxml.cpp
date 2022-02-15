/*
  Copyright (c) 2022 Georg Hajdu Permission is hereby granted,
  free of charge, to any person obtaining a copy of this software
  and associated documentation files (the "Software"), to deal in
  the Software without restriction, including without limitation the
  rights to use, copy, modify, merge, publish, distribute,
  sublicense, and/or sell copies of the Software, and to permit
  persons to whom the Software is furnished to do so, subject to the
  following conditions:

  The above copyright notice and this permission notice shall be
  included in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
  HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
  WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
  DEALINGS IN THE SOFTWARE.
*/

#include "ext.h"
#include "ext_obex.h"
// for postdictionary for debugging
#include "jpatcher_utils.h"
#include "ext_critical.h"
#include "ext_dictobj.h"

#include "rapidxml.hpp"

#include <assert.h>

#define RXML_OUTLET_MAIN 0

void *rxml_class;

t_symbol *ps_dictionary;

using namespace rapidxml;

typedef struct _rxml
{
	t_object ob;
    void *outlets[1];
    t_critical lock;
    char *buf;
    size_t buflen, bufpos;
    char *root;
    size_t rootlen;
    int rootcount;
} rxml;

extern "C" {
static void rxml_anything(rxml *x,
                          const t_symbol * const s,
                          const long ac, const t_atom *av)
{
    assert(x);
    assert(s);
    const char * const str = s->s_name;
    assert(str);
    const size_t len = strlen(str);
    critical_enter(x->lock);
    size_t buflen = x->buflen;
    size_t bufpos = x->bufpos;
    critical_exit(x->lock);
    if(len + bufpos >= buflen)
    {
        buflen += 250000;
        char *buf = (char *)realloc(x->buf, x->buflen);
        if(buf)
        {
            object_error((t_object *)x, "Out of memory!\n");
            critical_exit(x->lock);
            return;
        }
        critical_enter(x->lock);
        x->buflen = buflen;
        x->buf = buf;
        critical_exit(x->lock);
    }
    critical_enter(x->lock);
    memcpy(x->buf + bufpos, str, len);
    x->bufpos += len;
    critical_exit(x->lock);

    
}

static void rxml_dictionary(rxml *x, t_symbol *s)
{

}

static void rxml_toJSON(rxml *x, const xml_node<> *node,
                        t_dictionary *d, long index)
{
    assert(node);
    assert(d);
    const node_type t = node->type();
    switch(t)
    {
    case node_element:
    {
        // printf("node_element: %s\n", node->name());
        t_dictionary *thiselem = dictionary_new();
        t_symbol *thiselem_name = gensym(node->name());        
        if(dictionary_hasentry(d, thiselem_name))
        {
            t_dictionary *parent = NULL;
            t_max_err e = dictionary_getdictionary(d, thiselem_name,
                                                   (t_object **)&parent);
            if(e)
            {
                object_error((t_object *)x, "Error converting to JSON");
                return;
            }
            long nkeys = dictionary_getentrycount(parent);
            char k[32];
            snprintf(k, 32, "%d", nkeys);
            dictionary_appenddictionary(parent, gensym(k), (t_object *)thiselem);
        }
        else
        {
            t_dictionary *dd = dictionary_new();
            dictionary_appenddictionary(d, thiselem_name, (t_object *)dd);
            dictionary_appenddictionary(dd, gensym("0"), (t_object *)thiselem);
        }
        
        for(const xml_attribute<> *a = node->first_attribute();
            a;
            a = a->next_attribute())
        {
            // printf("node_element: %s: attribute: %s, %s\n", node->name(), a->name(), a->value());
            size_t n = strlen(a->name());
            char key[n + 2];
            snprintf(key, n + 2, "@%s", a->name());
            dictionary_appendstring(thiselem, gensym(key), a->value());
        }

        long i = 0;
        for(const xml_node<> *n = node->first_node();
            n;
            n = n->next_sibling(), ++i)
        {
            rxml_toJSON(x, n, thiselem, i);
        }
    }
    break;
    case node_data:
        // printf("node_data: %s\n", node->value());
        break;
    default:
        object_error((t_object *)x,
                     "Encountered unexpected node type: %d",
                     t);
        return;
    }
}

static void rxml_bang(rxml *x)
{
    critical_enter(x->lock);
    size_t bufpos = x->bufpos;
    critical_exit(x->lock);
    if(!bufpos)
    {
        return;
    }
    char *buf = (char *)malloc(bufpos + 1);
    if(!buf)
    {
        object_error((t_object *)x,
                     "Couldn't allocate memory for temporary buffer");
        return;
    }
    critical_enter(x->lock);
    memcpy(buf, x->buf, bufpos + 1);
    critical_exit(x->lock);
    buf[bufpos] = 0;
    
    xml_document<> doc;
    doc.parse<0>(buf);

    xml_node<> *root = doc.first_node();
    if(!root)
    {
        object_error((t_object *)x, "No root!");
    }
    else
    {
        t_dictionary *d = dictionary_new();
        rxml_toJSON(x, root, d, 0);
        // postdictionary((t_object *)d);
        // dictionary_dump(d, 1, 0);
        t_symbol *name = NULL;
        t_dictionary *dd = dictobj_register(d, &name);
        if(!dd || !name)
        {
            object_error((t_object *)x, "Couldn't register dict");
            goto cleanup;
        }
        object_post((t_object *)x, "%p %p\n", d, dd);
        t_atom out;
        atom_setsym(&out, name);
        outlet_anything(x->outlets[RXML_OUTLET_MAIN],
                        ps_dictionary, 1, &out);
        dictobj_release(dd);
    }

cleanup:
    critical_enter(x->lock);
    memset(x->buf, 0, x->bufpos);
    x->bufpos = 0;
    critical_exit(x->lock);
}

static void rxml_free(rxml *x)
{
    critical_free(x->lock);
}

static void rxml_assist(rxml *x, void *b, long m, long a, char *s)
{
	if(m==1)
    {
        // inlets
		switch(a){
		case 0:
            sprintf(s, "Inlet");
            break;
		}
	}
	else if(m==2)
    {
		switch(a)
        {
		case 0:
            sprintf(s, "Outlet");
            break;
		}
	}
}

static void *rxml_new(t_symbol *sym, long ac, t_atom *av)
{
    rxml *x = (rxml *)object_alloc((t_class *)rxml_class);
    if(x == NULL)
    {
        return NULL;
    }
    critical_new(&(x->lock));
    x->outlets[RXML_OUTLET_MAIN] = outlet_new((t_object *)x, NULL);
    x->buf = (char *)calloc(1000000, 1);
    if(!x->buf)
    {
        object_error((t_object *)x, "Couldn't allocate memory");
        return NULL;
    }
    x->buflen = 1000000;
    x->bufpos = 0;
	return x;
}

void ext_main(void *r)
{
	t_class *c = class_new("rxml",
                           (method)rxml_new,
                           (method)rxml_free,
                           (short)sizeof(rxml),
                           0L, A_GIMME, 0);

    class_addmethod(c, (method)rxml_anything, "anything", A_GIMME, 0);
    class_addmethod(c, (method)rxml_bang, "bang", 0);
    class_addmethod(c, (method)rxml_dictionary, "dictionary", A_SYM, 0);
	class_addmethod(c, (method)rxml_assist,	"assist", A_CANT, 0);

	class_register(CLASS_BOX, c);
	rxml_class = c;
    ps_dictionary = gensym("dictionary");
}

} // extern "C"
