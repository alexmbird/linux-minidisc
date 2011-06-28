/* libnetmd.c
 *      Copyright (C) 2002, 2003 Marc Britten
 *
 * This file is part of libnetmd.
 *
 * libnetmd is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Libnetmd is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#include "libnetmd.h"

int min(int a,int b)
{
    if (a<b) return a;
    return b;
}

/*! list of known codecs (mapped to protocol ID) that can be used in NetMD devices */
/*! Bertrik: the original interpretation of these numbers as codecs appears incorrect.
  These values look like track protection values instead */
struct netmd_pair const trprot_settings[] = 
{
    {0x00, "UnPROT"},
    {0x03, "TrPROT"},
    {0, 0} /* terminating pair */
};

/*! list of known bitrates (mapped to protocol ID) that can be used in NetMD devices */
struct netmd_pair const bitrates[] =
{
    {0x90, "Stereo"},
    {0x92, "LP2"},
    {0x93, "LP4"},
    {0, 0} /* terminating pair */
};

struct netmd_pair const unknown_pair = {0x00, "UNKNOWN"};

struct netmd_pair const* find_pair(int hex, struct netmd_pair const* array)
{
    int i = 0;
    for(; array[i].name != NULL; i++)
    {
        if(array[i].hex == hex)
            return &array[i];
    }

    return &unknown_pair;
}

static void waitforsync(usb_dev_handle* dev)
{
    char syncmsg[4];
    fprintf(stderr,"Waiting for Sync: \n");
    do {
        usb_control_msg(dev, 0xc1, 0x01, 0, 0, syncmsg, 0x04, 5000);
    } while  (memcmp(syncmsg,"\0\0\0\0",4)!=0);

}

static char* sendcommand(netmd_dev_handle* devh, char* str, int len, unsigned char* response, int rlen)
{
    int i, ret, size = 0;
    static char buf[256];

    ret = netmd_exch_message(devh, str, len, buf);
    if (ret < 0) {
        fprintf(stderr, "bad ret code, returning early\n");
        return NULL;
    }

    // Calculate difference to expected response
    if (response != NULL) {
        int c=0;
        for (i=0; i < min(rlen, size); i++) {
            if (response[i] != buf[i]) {
                c++;
            }
        }
        fprintf(stderr, "Differ: %d\n",c);
    }

    return buf;
}

static int request_disc_title(netmd_dev_handle* dev, char* buffer, int size)
{
    int ret = -1;
    int title_size = 0;
    char title_request[] = {0x00, 0x18, 0x06, 0x02, 0x20, 0x18,
                            0x01, 0x00, 0x00, 0x30, 0x00, 0xa,
                            0x00, 0xff, 0x00, 0x00, 0x00, 0x00,
                            0x00};
    char title[255];

    ret = netmd_exch_message(dev, title_request, 0x13, title);
    if(ret < 0)
    {
        fprintf(stderr, "request_disc_title: bad ret code, returning early\n");
        return 0;
    }

    title_size = ret;

    if(title_size == 0 || title_size == 0x13)
        return -1; /* bail early somethings wrong */

    if(title_size > size)
    {
        printf("request_disc_title: title too large for buffer\n");
        return -1;
    }

    memset(buffer, 0, size);
    memcpy(buffer, (title + 25), title_size - 25);
    /* buffer[size + 1] = 0;   XXX Huh? This is outside the bounds passed in! */

    return title_size - 25;
}

int netmd_request_track_bitrate(netmd_dev_handle*dev, int track, unsigned char* data)
{
    int ret = 0;
    int size = 0;
    char request[] = {0x00, 0x18, 0x06, 0x02, 0x20, 0x10,
                      0x01, 0x00, 0xDD, 0x30, 0x80, 0x07,
                      0x00, 0xff, 0x00, 0x00, 0x00, 0x00,
                      0x00};
    char reply[255];

    request[8] = track;
    ret = netmd_exch_message(dev, request, 0x13, reply);
    if(ret < 0)
    {
        fprintf(stderr, "bad ret code, returning early\n");
        return 0;
    }

    size = ret;

    *data = reply[size - 2];
    return ret;
}

int netmd_request_track_flags(netmd_dev_handle*dev, int track, char* data)
{
    int ret = 0;
    int size = 0;
    char request[] = {0x00, 0x18, 0x06, 0x01, 0x20, 0x10,
                      0x01, 0x00, 0xDD, 0xff, 0x00, 0x00,
                      0x01, 0x00, 0x08};
    char reply[255];

    request[8] = track;
    ret = netmd_exch_message(dev, request, 15, reply);
    if(ret < 0)
    {
        fprintf(stderr, "bad ret code, returning early\n");
        return 0;
    }

    size = ret;

    *data = reply[size - 1];
    return ret;
}

/*  equiv. of
    sprintf(tmp, "%02x", time_request[ 	time = strtol(tmp, NULL, 10); */
#define BCD_TO_PROPER(x) (((((x) & 0xf0) >> 4) * 10) + ((x) & 0x0f))

int netmd_request_track_time(netmd_dev_handle* dev, int track, struct netmd_track* buffer)
{
    int ret = 0;
    int size = 0;
    char request[] = {0x00, 0x18, 0x06, 0x02, 0x20, 0x10,
                      0x01, 0x00, 0x01, 0x30, 0x00, 0x01,
                      0x00, 0xff, 0x00, 0x00, 0x00, 0x00,
                      0x00};
    char time_request[255];

    request[8] = track;
    ret = netmd_exch_message(dev, request, 0x13, time_request);
    if(ret < 0)
    {
        fprintf(stderr, "bad ret code, returning early\n");
        return 0;
    }

    size = ret;

    buffer->minute = BCD_TO_PROPER(time_request[28]);
    buffer->second = BCD_TO_PROPER(time_request[29]);
    buffer->tenth = BCD_TO_PROPER(time_request[30]);
    buffer->track = track;

    return 1;
}

int netmd_request_title(netmd_dev_handle* dev, int track, char* buffer, int size)
{
    int ret = -1;
    int title_size = 0;
    char title_request[] = {0x00, 0x18, 0x06, 0x02, 0x20, 0x18,
                            0x02, 0x00, 0x00, 0x30, 0x00, 0xa,
                            0x00, 0xff, 0x00, 0x00, 0x00, 0x00,
                            0x00};
    char title[255];

    title_request[8] = track;
    ret = netmd_exch_message(dev, title_request, 0x13, title);
    if(ret < 0)
    {
        fprintf(stderr, "bad ret code, returning early\n");
        return -1;
    }

    title_size = ret;

    if(title_size == 0 || title_size == 0x13)
        return -1; /* bail early somethings wrong or no track */

    if(title_size > size)
    {
        printf("netmd_request_title: title too large for buffer\n");
        return -1;
    }

    memset(buffer, 0, size);
    memcpy(buffer, (title + 25), title_size - 25);
    buffer[size + 1] = 0;

    return title_size - 25;
}

int netmd_set_title(netmd_dev_handle* dev, int track, char* buffer)
{
    int ret = 1;
    char *title_request = NULL;
    char title_header[] = {0x00, 0x18, 0x07, 0x02, 0x20, 0x18,
                           0x02, 0x00, 0x00, 0x30, 0x00, 0x0a,
                           0x00, 0x50, 0x00, 0x00, 0x0a, 0x00,
                           0x00, 0x00, 0x0d};
    char reply[255];
    int size;

    size = strlen(buffer);
    title_request = malloc(sizeof(char) * (0x15 + size));
    memcpy(title_request, title_header, 0x15);
    memcpy((title_request + 0x15), buffer, size);

    title_request[8] = track;
    title_request[16] = size;
    title_request[20] = size;

    ret = netmd_exch_message(dev, title_request, (int)(0x15 + size), reply);
    if(ret < 0)
    {
        fprintf(stderr, "bad ret code, returning early\n");
        return 0;
    }

    free(title_request);
    return 1;
}

int netmd_move_track(netmd_dev_handle* dev, int start, int finish)
{
    int ret = 0;
    char request[] = {0x00, 0x18, 0x43, 0xff, 0x00, 0x00,
                      0x20, 0x10, 0x01, 0x00, 0x04, 0x20,
                      0x10, 0x01, 0x00, 0x03};
    char reply[255];

    request[10] = start;
    request[15] = finish;

    ret = netmd_exch_message(dev, request, 16, reply);

    if(ret < 0)
    {
        fprintf(stderr, "bad ret code, returning early\n");
        return 0;
    }

    return 1;
}

static int get_group_count(netmd_dev_handle* devh)
{
    char title[256];
    size_t title_length;
    char *group;
    char *delim;
    int group_count = 1;

    title_length = request_disc_title(devh, title, 256);

    group = title;
    delim = strstr(group, "//");

    while (delim < (title + title_length))
    {
        if (delim != NULL)
        {
            // if delimiter was found
            delim[0] = '\0';
        }

        if (strlen(group) > 0) {
            if (atoi(group) > 0 || group[0] == ';') {
                group_count++;
            }
        }

        if (NULL == delim)
        {
            // finish if delimiter was not found the last time
            break;
        }

        if (delim+2 > title+title_length)
        {
            // finish if delimiter was at end of title
            break;
        }

        group = delim + 2;
        delim = strstr(group, "//");
    }

    return group_count;
}

int netmd_set_group_title(netmd_dev_handle* dev, minidisc* md, int group, char* title)
{
    int size = strlen(title);

    md->groups[group].name = realloc(md->groups[group].name, size + 1);

    if(md->groups[group].name != 0)
        strcpy(md->groups[group].name, title);
    else
        return 0;

    netmd_write_disc_header(dev, md);

    return 1;
}

static void set_group_data(minidisc* md, int group, char* name, int start, int finish) {
    md->groups[group].name = strdup( name );
    md->groups[group].start = start;
    md->groups[group].finish = finish;
    return;
}

/* Sonys code is utter bile. So far we've encountered the following first segments in the disc title:
 *
 * 0[-n];<title>// - titled disc.
 * <title>// - titled disc
 * 0[-n];// - untitled disc
 * n{n>0};<group>// - untitled disc, group with one track
 * n{n>0}-n2{n2>n>0};group// - untitled disc, group with multiple tracks
 * ;group// - untitled disc, group with no tracks
 *
 */

int netmd_initialize_disc_info(netmd_dev_handle* devh, minidisc* md)
{
    int disc_size = 0;
    char disc[256];

    md->group_count = get_group_count(devh);

    /* You always have at least one group, the disc title */
    if(md->group_count == 0)
        md->group_count++;

    md->groups = malloc(sizeof(struct netmd_group) * md->group_count);
    memset(md->groups, 0, sizeof(struct netmd_group) * md->group_count);

    disc_size = request_disc_title(devh, disc, 256);
    printf("Raw title: %s \n", disc);
    md->header_length = disc_size;

    if(disc_size != 0)
    {
        netmd_parse_disc_title(md, disc, disc_size);
    }

    if (NULL == md->groups[0].name)
    {
        // set untitled disc title
        set_group_data(md, 0, "<Untitled>", 0, 0);
    }

    return disc_size;
}

void netmd_parse_disc_title(minidisc* md, char* title, size_t title_length)
{
    char *group;
    char *delim;
    int group_count = 1;

    group = title;
    delim = strstr(group, "//");

    while (delim < (title + title_length))
    {
        if (delim != NULL)
        {
            // if delimiter was found
            delim[0] = '\0';
        }

        netmd_parse_group(md, group, &group_count);

        if (NULL == delim)
        {
            // finish if delimiter was not found the last time
            break;
        }

        group = delim + 2;

        if (group > (title + title_length))
        {
            // end of title
            break;
        }

        delim = strstr(group, "//");
    }
}

void netmd_parse_group(minidisc* md, char* group, int* group_count)
{
    char *group_name;

    group_name = strchr(group, ';');
    if (NULL == group_name)
    {
        if (strlen(group) > 0)
        {
            // disc title
            set_group_data(md, 0, group, 0, 0);
        }
    }
    else
    {
        group_name[0] = '\0';
        group_name++;

        if (strlen(group_name) > 0)
        {
            if (0 == strlen(group))
            {
                set_group_data(md, *group_count, group_name, 0, 0);
                (*group_count)++;
            }
            else
            {
                netmd_parse_trackinformation(md, group_name, group_count, group);
            }
        }
    }
}

void netmd_parse_trackinformation(minidisc* md, char* group_name, int* group_count, char* tracks)
{
    char *track_last;
    int start, finish;

    start = atoi(tracks);
    if (start == 0)
    {
        // disc title
        set_group_data(md, 0, group_name, 0, 0);
    }
    else {
        track_last = strchr(tracks, '-');

        if (NULL == track_last)
        {
            finish = start;
        }
        else
        {
            track_last[0] = '\0';
            track_last++;

            finish = atoi(track_last);
        }

        set_group_data(md, *group_count, group_name, start, finish);
        (*group_count)++;
    }
}

void print_groups(minidisc *md)
{
    int i = 0;
    for(;i < md->group_count; i++)
    {
        printf("Group %i: %s - %i - %i\n", i, md->groups[i].name, md->groups[i].start, md->groups[i].finish);
    }
    printf("\n");
}

int netmd_create_group(netmd_dev_handle* dev, minidisc* md, char* name)
{
    int new_index;

    new_index = md->group_count;
    md->group_count++;
    md->groups = realloc(md->groups, sizeof(struct netmd_group) * (md->group_count + 1));

    md->groups[new_index].name = strdup(name);
    md->groups[new_index].start = 0;
    md->groups[new_index].finish = 0;

    netmd_write_disc_header(dev, md);
    return 0;
}

int netmd_set_disc_title(netmd_dev_handle* dev, char* title, size_t title_length)
{
    char *request, *p;
    char write_req[] = {0x00, 0x18, 0x07, 0x02, 0x20, 0x18,
                        0x01, 0x00, 0x00, 0x30, 0x00, 0x0a,
                        0x00, 0x50, 0x00, 0x00};
    char reply[256];
    int result;

    request = malloc(21 + title_length);
    memset(request, 0, 21 + title_length);

    memcpy(request, write_req, 16);
    request[16] = title_length;
    request[20] = title_length;

    p = request + 21;
    memcpy(p, title, title_length);

    result = netmd_exch_message(dev, request, 0x15 + title_length, reply);
    return result;
}

/* move track, then manipulate title string */
int netmd_put_track_in_group(netmd_dev_handle* dev, minidisc *md, int track, int group)
{
    int i = 0;
    int j = 0;
    int found = 0;

    printf("netmd_put_track_in_group(dev, %i, %i)\nGroup Count %i\n", track, group, md->group_count);

    if(group >= md->group_count)
    {
        return 0;
    }

    print_groups(md);

    /* remove track from old group */
    for(i = 0; i < md->group_count; i++)
    {
        if(i == 0)
        {
            /* if track is before first real group */
            if(md->groups[i+1].start == 0)
            {
                /* nothing in group  */
                found = 1;
            }
            if((track + 1) < md->groups[i+1].start)
            {
                found = 1;
                for(j = i+1; j < md->group_count; j++)
                {
                    md->groups[j].start--;
                    if(md->groups[j].finish != 0)
                        md->groups[j].finish--;
                }
            }
        }
        else if(md->groups[i].start <= (track + 1) && md->groups[i].finish >= (track + 1))
        {
            found = 1;
            /* decrement start/finish for all following groups */
            for(j = i+1; j < md->group_count; j++)
            {
                md->groups[j].start--;
                if(md->groups[j].finish != 0)
                    md->groups[j].finish--;
            }
        }
    }

    /* if track is in between groups */
    if(!found)
    {
        for(i = 2; i < md->group_count; i++)
        {
            if(md->groups[i].start >= (track+1) && md->groups[i-1].finish <= (track+1))
            {
                found = 1;
                /* decrement start/finish for all groups including and after this one */
                for(j = i; j < md->group_count; j++)
                {
                    md->groups[j].start--;
                    if(md->groups[j].finish != 0)
                        md->groups[j].finish--;
                }
            }
        }
    }

    print_groups(md);

    /* insert track into group range */
    if(md->groups[group].finish != 0)
    {
        md->groups[group].finish++;
    }
    else
    {
        if(md->groups[group].start == 0)
            md->groups[group].start = track + 1;
        else
            md->groups[group].finish = md->groups[group].start + 1;
    }

    /* if not last group */
    if((group + 1) < md->group_count)
    {
        int j = 0;
        for(j = group + 1; j < md->group_count; j++)
        {
            /* if group is NOT empty */
            if(md->groups[j].start != 0 || md->groups[j].finish != 0)
            {
                md->groups[j].start++;
                if(md->groups[j].finish != 0)
                {
                    md->groups[j].finish++;
                }
            }
        }
    }

    /* what does it look like now? */
    print_groups(md);

    if(md->groups[group].finish != 0)
    {
        netmd_move_track(dev, track, md->groups[group].finish - 1);
    }
    else
    {
        if(md->groups[group].start != 0)
            netmd_move_track(dev, track, md->groups[group].start - 1);
        else
            netmd_move_track(dev, track, md->groups[group].start);
    }

    return netmd_write_disc_header(dev, md);
}

int netmd_move_group(netmd_dev_handle* dev, minidisc* md, int track, int group)
{
    int index = 0;
    int i = 0;
    int gs = 0;
    struct netmd_group store1;
    struct netmd_group *p, *p2;
    int gt = md->groups[group].start;
    int finish = (md->groups[group].finish - md->groups[group].start) + track;

    p = p2 = 0;

    // empty groups can't be in front
    if(gt == 0)
        return -1;

    /* loop, moving tracks to new positions */
    for(index = track; index <= finish; index++, gt++)
    {
        printf("Moving track %i to %i\n", (gt - 1), index);
        netmd_move_track(dev, (gt -1), index);
    }
    md->groups[group].start = track + 1;
    md->groups[group].finish = index;

    /* create a copy of groups */
    p = malloc(sizeof(struct netmd_group) * md->group_count);
    for(index = 0; index < md->group_count; index++)
    {
        p[index].name = malloc(strlen(md->groups[index].name) + 1);
        strcpy(p[index].name, md->groups[index].name);
        p[index].start = md->groups[index].start;
        p[index].finish = md->groups[index].finish;
    }

    store1 = p[group];
    gs = store1.finish - store1.start + 1; /* how many tracks got moved? */

    /* find group to bump */
    if(track < md->groups[group].start)
    {
        for(index = 0; index < md->group_count; index++)
        {
            if(md->groups[index].start > track)
            {
                for(i = group - 1; i >= index; i--)
                {
                    /* all tracks get moved gs spots */
                    p[i].start += gs;

                    if(p[i].finish != 0)
                        p[i].finish += gs;

                    p[i + 1] = p[i]; /* bump group down the list */
                }

                p[index] = store1;
                break;
            }
            else
            {
                if((group + 1) < md->group_count)
                {
                    for(i = group + 1; i < md->group_count; i++)
                    {
                        /* all tracks get moved gs spots */
                        p[i].start -= gs;

                        if(p[i].finish != 0)
                            p[i].finish -= gs;

                        p[i - 1] = p[i]; /* bump group down the list */
                    }

                    p[index] = store1;
                    break;
                }
            }
        }
    }

    /* free all memory, then make our copy the real info */
    netmd_clean_disc_info(md);
    md->groups = p;

    netmd_write_disc_header(dev, md);
    return 0;
}

int netmd_delete_group(netmd_dev_handle* dev, minidisc* md, int group)
{
    int index = 0;
    struct netmd_group *p;

    /* check if requested group exists */
    if((group < 0) || (group > md->group_count))
        return -1;

    /* create a copy of groups below requested group */
    p = malloc(sizeof(struct netmd_group) * md->group_count);
    for(index = 0; index < group; index++)
    {
        p[index].name = strdup(md->groups[index].name);
        p[index].start = md->groups[index].start;
        p[index].finish = md->groups[index].finish;
    }

    /* copy groups above requested group */
    for(; index < md->group_count-1; index++)
    {
        p[index].name = strdup(md->groups[index+1].name);
        p[index].start = md->groups[index+1].start;
        p[index].finish = md->groups[index+1].finish;
    }

    /* free all memory, then make our copy the real info */
    netmd_clean_disc_info(md);
    md->groups = p;

    /* one less group now */
    md->group_count--;

    netmd_write_disc_header(dev, md);
    return 0;
}

int netmd_calculate_disc_header_length(minidisc* md)
{
    int i;
    size_t header_size;

    if (md->groups[0].start == 0)
    {
        header_size = 1;
    }
    else
    {
        header_size = 0;
    }

    /* calculate header length */
    for(i = 0; i < md->group_count; i++)
    {
        if(md->groups[i].start > 0)
        {
            if(md->groups[i].start < 100)
            {
                if(md->groups[i].start < 10)
                    header_size += 1;
                else
                    header_size += 2;
            }
            else
                header_size += 3;

            if(md->groups[i].finish != 0)
            {
                if(md->groups[i].start < 100)
                {
                    if(md->groups[i].start < 10)
                        header_size += 1;
                    else
                        header_size += 2;
                }
                else
                    header_size += 3;

                header_size++; /* room for the - */
            }
        }

        header_size += 3; /* room for the ; and // tokens */
        header_size += strlen(md->groups[i].name);
    }

    header_size++;
    return header_size;
}

size_t netmd_calculate_remaining(char** position, size_t remaining, size_t written)
{
    if (remaining > written)
    {
        (*position) += written;
        remaining -= written;
    }
    else
    {
        (*position) += remaining;
        remaining = 0;
    }

    return remaining;
}

char* netmd_generate_disc_header(minidisc* md, char* header, size_t header_length)
{
    int i, remaining, written;
    char* position;

    position = header;
    remaining = header_length - 1;

    if (md->groups[0].start == 0)
    {
        strncat(position, "0", remaining);
        written = strlen(position);
        remaining = netmd_calculate_remaining(&position, remaining, written);
    }

    for(i = 0; i < md->group_count; i++)
    {
        if(md->groups[i].start > 0)
        {
            written = snprintf(position, remaining, "%d", md->groups[i].start);
            remaining = netmd_calculate_remaining(&position, remaining, written);

            if(md->groups[i].finish != 0)
            {
                written = snprintf(position, remaining, "-%d", md->groups[i].finish);
                remaining = netmd_calculate_remaining(&position, remaining, written);
            }
        }

        written = snprintf(position, remaining, ";%s//", md->groups[i].name);
        remaining = netmd_calculate_remaining(&position, remaining, written);
    }

    position[0] = '\0';
    return header;
}

int netmd_write_disc_header(netmd_dev_handle* devh, minidisc* md)
{
    size_t header_size;
    size_t request_size;
    char* header = 0;
    char* request = 0;
    char write_req[] = {0x00, 0x18, 0x07, 0x02, 0x20, 0x18,
                        0x01, 0x00, 0x00, 0x30, 0x00, 0x0a,
                        0x00, 0x50, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00};
    char reply[255];
    int ret;

    header_size = netmd_calculate_disc_header_length(md);
    header = malloc(sizeof(char) * header_size);
    memset(header, 0, header_size);

    netmd_generate_disc_header(md, header, header_size);

    request_size = header_size + sizeof(write_req);
    request = malloc(request_size);
    memset(request, 0, request_size);

    memcpy(request, write_req, sizeof(write_req));
    request[16] = (header_size - 1); /* new size - null */
    request[20] = md->header_length; /* old size */

    memcpy(request + sizeof(write_req), header, header_size);

    ret = netmd_exch_message(devh, request, request_size, reply);
    free(request);

    return ret;
}


int netmd_write_track(netmd_dev_handle* devh, char* szFile)
{
    int ret = 0;
    int fd = open(szFile, O_RDONLY); /* File descriptor to omg file */
    char *data = malloc(4096); /* Buffer for reading the omg file */
    char *p = NULL; /* Pointer to index into data */
    char track_number='\0'; /* Will store the track number of the recorded song */

    /* Some unknown command being send before titling */
    char begintitle[] = {0x00, 0x18, 0x08, 0x10, 0x18, 0x02,
                         0x03, 0x00};

    /* Some unknown command being send after titling */
    char endrecord[] =  {0x00, 0x18, 0x08, 0x10, 0x18, 0x02,
                         0x00, 0x00};

    /* Command to finish toc flashing */
    char fintoc[] = {0x00, 0x18, 0x00, 0x08, 0x00, 0x46,
                     0xf0, 0x03, 0x01, 0x03, 0x48, 0xff,
                     0x00, 0x10, 0x01, 0x00, 0x25, 0x8f,
                     0xbf, 0x09, 0xa2, 0x2f, 0x35, 0xa3,
                     0xdd};

    /* Record command */
    char movetoendstartrecord[] = {0x00, 0x18, 0x00, 0x08, 0x00, 0x46,
                                   0xf0, 0x03, 0x01, 0x03, 0x28, 0xff,
                                   0x00, 0x01, 0x00, 0x10, 0x01, 0xff,
                                   0xff, 0x00, 0x94, 0x02, 0x00, 0x00,
                                   0x00, 0x06, 0x00, 0x00, 0x04, 0x98};

    /* The expected response from the record command. */
    unsigned char movetoendresp[] = {0x0f, 0x18, 0x00, 0x08, 0x00, 0x46,
                                     0xf0, 0x03, 0x01, 0x03, 0x28, 0x00,
                                     0x00, 0x01, 0x00, 0x10, 0x01, 0x00,
                                     0x11, 0x00, 0x94, 0x02, 0x00, 0x00,
                                     0x43, 0x8c, 0x00, 0x32, 0xbc, 0x50};

    /* Header to be inserted at every 0x3F10 bytes */
    char header[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                     0x3f, 0x00, 0xd4, 0x4b, 0xdc, 0xaa,
                     0xef, 0x68, 0x22, 0xe2};

    //	char debug[]  =      {0x4c, 0x63, 0xa0, 0x20, 0x82, 0xae, 0xab, 0xa1};
    char size_request[4];
    int data_size_i; /* the size of the data part, later it will be used to point out the last byte in file */
    unsigned int size;
    char* buf=NULL; /* A buffer for recieving file info */
    usb_dev_handle	*dev;

    dev = (usb_dev_handle *)devh;

    if(fd < 0)
        return fd;

    if(!data)
        return ENOMEM;


    /********** Get the size of file ***********/
    lseek(fd, 0x56, SEEK_SET); // Read to data size
    read(fd,data,4);
    data_size_i =0;
    data_size_i = data[3];
    data_size_i += data[2] << 8;
    data_size_i += data[1] << 16;
    data_size_i += data[0] << 24;

    fprintf(stderr,"Size of data: %d\n",data_size_i);
    size = (data_size_i/0x3f18)*8+data_size_i+8;           // plus number of data headers
    fprintf(stderr,"Size of data w/ headers: %d\n",size);


    /********** Fill in information in start record command and send ***********/
    // not sure if size is 3 of 4 bytes in rec command...
    movetoendstartrecord[27]=(size >> 16) & 255;
    movetoendstartrecord[28]=(size >> 8) & 255;
    movetoendstartrecord[29]=size & 255;

    buf = sendcommand(devh, movetoendstartrecord, 30, movetoendresp, 0x1e);
    track_number = buf[0x12];


    /********** Prepare to send data ***********/
    lseek(fd, 90, SEEK_SET);  /* seek to 8 bytes before leading 8 00's */
    data_size_i += 90;        /* data_size_i will now contain position of last byte to be send */

    waitforsync(dev);   /* Wait for 00 00 00 00 from unit.. */


    /********** Send data ***********/
    while(ret >= 0)
    {
        int file_pos=0;	/* Position in file */
        int bytes_to_send;    /* The number of bytes that needs to be send in this round */

        int __bytes_left;     /* How many bytes are left in the file */
        int __chunk_size;     /* How many bytes are left in the 0x1000 chunk to send */
        int __distance_to_header; /* How far till the next header insert */

        file_pos = lseek(fd,0,SEEK_CUR); /* Gets the position in file, might be a nicer way of doing this */

        fprintf(stderr,"pos: %d/%d; remain data: %d\n",file_pos,data_size_i,data_size_i-file_pos);
        if (file_pos >= data_size_i) {
            fprintf(stderr,"Done transfering\n");
            free(data);
            break;
        }

        __bytes_left = data_size_i-file_pos;
        __chunk_size = min(0x1000,__bytes_left);
        __distance_to_header = (file_pos-0x5a) % 0x3f10;
        if (__distance_to_header !=0) __distance_to_header = 0x3f10 - __distance_to_header;
        bytes_to_send = __chunk_size;

        fprintf(stderr,"Chunksize: %d\n",__chunk_size);
        fprintf(stderr,"distance_to_header: %d\n",__distance_to_header);
        fprintf(stderr,"Bytes left: %d\n",__bytes_left);

        if (__distance_to_header <= 0x1000) {   	     /* every 0x3f10 the header should be inserted, with an appropiate key.*/
            fprintf(stderr,"Inserting header\n");

            if (__chunk_size<0x1000) {                 /* if there is room for header then make room for it.. */
                __chunk_size += 0x10;              /* Expand __chunk_size */
                bytes_to_send = __chunk_size-0x08; /* Expand bytes_to_send */
            }

            read(fd,data, __distance_to_header ); /* Errors checking should be added for read function */
            __chunk_size -= __distance_to_header; /* Update chunk size */

            p = data+__distance_to_header;        /* Change p to point at the position header should be inserted */
            memcpy(p,header,16);
            __bytes_left = min(0x3f00, __bytes_left-__distance_to_header-0x10);
            fprintf (stderr, "bytes left in chunk: %d\n",__bytes_left);
            p[6] = __bytes_left >> 8;      /* Inserts the higher 8 bytes of the length */
            p[7] = __bytes_left & 255;     /* Inserts the lower 8 bytes of the length */
            __chunk_size -= 0x10;          /* Update chunk size (for inserted header */

            p += 0x10;                     /* p should now point at the beginning of the next data segment */
            lseek(fd,8,SEEK_CUR);          /* Skip the key value in omg file.. Should probably be used for generating the header */
            read(fd,p, __chunk_size);      /* Error checking should be added for read function */

        } else {
            if(0 == read(fd, data, __chunk_size)) { /* read in next chunk */
                free(data);
                break;
            }
        }

        netmd_trace(NETMD_TRACE_INFO, "Sending %d bytes to md\n", bytes_to_send);
        netmd_trace_hex(NETMD_TRACE_INFO, data, min(0x4000, bytes_to_send));
        ret = usb_bulk_write(dev,0x02, data, bytes_to_send, 5000);
    } /* End while */

    if (ret<0) {
        free(data);
        return ret;
    }


    /******** End transfer wait for unit ready ********/
    fprintf(stderr,"Waiting for Done:\n");
    do {
        usb_control_msg(dev, 0xc1, 0x01, 0, 0, size_request, 0x04, 5000);
    } while  (memcmp(size_request,"\0\0\0\0",4)==0);

    netmd_trace(NETMD_TRACE_INFO, "Recieving response: \n");
    netmd_trace_hex(NETMD_TRACE_INFO, size_request, 4);
    size = size_request[2];
    if (size<1) {
        fprintf(stderr, "Invalid size\n");
        return -1;
    }
    buf = malloc(size);
    usb_control_msg(dev, 0xc1, 0x81, 0, 0, buf, size, 500);
    netmd_trace_hex(NETMD_TRACE_INFO, buf, size);
    free(buf);

    /******** Title the transfered song *******/
    buf = sendcommand(devh, begintitle, 8, NULL, 0);

    fprintf(stderr,"Renaming track %d to test\n",track_number);
    netmd_set_title(devh, track_number, "test");

    buf = sendcommand(devh, endrecord, 8, NULL, 0);


    /********* End TOC Edit **********/
    ret = usb_control_msg(dev, 0x41, 0x80, 0, 0, fintoc, 0x19, 800);

    fprintf(stderr,"Waiting for Done: \n");
    do {
        usb_control_msg(dev, 0xc1, 0x01, 0, 0, size_request, 0x04, 5000);
    } while  (memcmp(size_request,"\0\0\0\0",4)==0);

    return ret;
}

int netmd_delete_track(netmd_dev_handle* dev, int track)
{
    int ret = 0;
    char request[] = {0x00, 0x18, 0x40, 0xff, 0x01, 0x00,
                      0x20, 0x10, 0x01, 0x00, 0x00};
    char reply[255];

    request[10] = track;
    ret = netmd_exch_message(dev, request, 11, reply);

    return ret;
}

int netmd_get_current_track(netmd_dev_handle* dev)
{
    int ret = 0;
    char request[] = {0x00, 0x18, 0x09, 0x80, 0x01, 0x04,
                      0x30, 0x88, 0x02, 0x00, 0x30, 0x88,
                      0x05, 0x00, 0x30, 0x00, 0x03, 0x00,
                      0x30, 0x00, 0x02, 0x00, 0xff, 0x00,
                      0x00, 0x00, 0x00, 0x00};
    char buf[255];
    int track = 0;

    ret = netmd_exch_message(dev, request, 28, buf);

    track = buf[36];

    return track;

}

float netmd_get_playback_position(netmd_dev_handle* dev)
{
    int ret = 0;
    char request[] = {0x00, 0x18, 0x09, 0x80, 0x01, 0x04,
                      0x30, 0x88, 0x02, 0x00, 0x30, 0x88,
                      0x05, 0x00, 0x30, 0x00, 0x03, 0x00,
                      0x30, 0x00, 0x02, 0x00, 0xff, 0x00,
                      0x00, 0x00, 0x00, 0x00};
    char buf[255];

    float position = 0.0f;

    int minutes = 0;
    int seconds = 0;
    int hundreds = 0;

    ret = netmd_exch_message(dev, request, 28, buf);

    minutes = BCD_TO_PROPER(buf[38]);
    seconds = BCD_TO_PROPER(buf[39]);
    hundreds = BCD_TO_PROPER(buf[40]);

    position = (minutes * 60) + seconds + ((float)hundreds / 100);

    if(position > 0)
    {
        return position;
    } else {
        return 0;
    }
}

void netmd_clean_disc_info(minidisc *md)
{
    int i = 0;
    for(; i < md->group_count; i++)
    {
        free(md->groups[i].name);
        md->groups[i].name = 0;
    }

    free(md->groups);

    md->groups = 0;
}
