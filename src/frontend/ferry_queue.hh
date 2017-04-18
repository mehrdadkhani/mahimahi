/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef FERRY_QUEUE_HH
#define FERRY_QUEUE_HH

#include "event_loop.hh"

class AbstractFerryQueue
{
public:
    virtual void register_events(EventLoop & event_loop);
};

#endif /* FERRY_QUEUE_HH */
