#include "NBV.h"

int NBV::view_bins_each_axis = 3;

NBV::NBV(RichParameterSet *_para)
{
  cout<<"NBV constructed!"<<endl;
  para = _para;
  original = NULL;
  iso_points = NULL;
  field_points = NULL;
  model = NULL;
}

NBV::~NBV()
{
  original = NULL;
  iso_points = NULL;
  model = NULL;
}

void
NBV::run()
{
  double camera_max_dist = global_paraMgr.camera.getDouble("Camera Max Dist");
  grid_resolution = camera_max_dist * 2. / para->getDouble("Grid resolution");

  if (para->getBool("Run Build Grid"))
  {
    buildGrid();
    return;
  }

  if (para->getBool("Run Propagate"))
  {
    propagate();
    return;
  }

  if (para->getBool("Run Viewing Clustering"))
  {
    viewClustering();
    return;
  }

  if (para->getBool("Run Viewing Extract"))
  {
    viewExtraction();
    return;
  }

  if (para->getBool("Run Extract Views Into Bins"))
  {
    viewExtractionIntoBins();
    return;
  }

  if (para->getBool("Run One Key NBV"))
  {
    runOneKeyNBV();
  }

  if (para->getBool("Run Set Iso Bottom Confidence"))
  {
    setIsoBottomConfidence();
  }

  if (para->getBool("Run Update View Directions"))
  {
    updateViewDirections();
  }
}

void 
NBV::runOneKeyNBV()
{
  buildGrid();
  propagate();
  viewExtractionIntoBins();

  //clear default scan_candidate
  scan_candidates->clear();
  //fix:for test, just scan the point who has the biggest confidence
  int max_idx = 0;
  float max_confidence = nbv_candidates->vert[0].eigen_confidence;
  for (int i = 0; i < nbv_candidates->vert.size(); ++i)
  {
	  if (nbv_candidates->vert[i].eigen_confidence > max_confidence)
		  max_idx = i;
  }

  ScanCandidate s = make_pair(nbv_candidates->vert[max_idx].P(), nbv_candidates->vert[max_idx].N());
  scan_candidates->push_back(s);
}

void
NBV::setInput(DataMgr *pData)
{
  if (!pData->getCurrentIsoPoints()->vert.empty())
  {
    CMesh *_original = pData->getCurrentOriginal();
    CMesh *_model = pData->getCurrentModel();

    if (NULL == _original)
    {
      cout<<"ERROR: NBV original == NULL !"<<endl;
      return;
    }
    if (NULL == _model)
    {
      cout<<"ERROR: NBV model == NULL !"<<endl;
      return;
    }
    model = _model;
    original = _original;
    field_points = pData->getCurrentFieldPoints();
  }else
  {
    cout<<"ERROR: NBV::setInput empty!"<<endl;
  }

  view_grid_points = pData->getAllNBVGridCenters();
  view_grids = pData->getAllNBVGrids();
  iso_points = pData->getCurrentIsoPoints();
  nbv_candidates = pData->getNbvCandidates();
  scan_candidates = pData->getAllScanCandidates();
}

void
NBV::clear()
{
  original = NULL;
}

void
NBV::buildGrid()
{
   bool use_grid_segment = para->getBool("Run Grid Segment");
  //fix: this should be model->bbox.max
   Point3f bbox_max = iso_points->bbox.max;
   Point3f bbox_min = iso_points->bbox.min;
   //get the whole 3D space that a camera may exist
   double camera_max_dist = global_paraMgr.camera.getDouble("Camera Max Dist");

   float scan_box_size = camera_max_dist + 0.5;
   whole_space_box_min = Point3f(-scan_box_size, -scan_box_size, -scan_box_size);
   whole_space_box_max = Point3f(scan_box_size, scan_box_size, scan_box_size);

   //compute the size of the 3D space
   Point3f dif = whole_space_box_max - whole_space_box_min;
   //divide the box into grid
   x_max = static_cast<int> (dif.X() / grid_resolution);
   y_max = static_cast<int> (dif.Y() / grid_resolution);
   z_max = static_cast<int> (dif.Z() / grid_resolution);

   int all_max = std::max(std::max(x_max, y_max), z_max);
   x_max = y_max =z_max = all_max;

   //preallocate the memory
   int max_index = x_max * y_max * z_max;
   view_grid_points->vert.resize(max_index);
   view_grids->resize(max_index);
   //increase from whole_space_box_min
   for (int i = 0; i < x_max; ++i)
   {
     for (int j = 0; j < y_max; ++j)
     {
       for (int k = 0; k < z_max; ++k)
       {
         //add the grid
         int index = i * y_max * z_max + j * z_max + k;
         NBVGrid grid(i, j, k);
         (*view_grids)[index] = grid;
         //add the center point of the grid
         CVertex t;
         t.P()[0] = whole_space_box_min.X() + i * grid_resolution;
         t.P()[1] = whole_space_box_min.Y() + j * grid_resolution;
         t.P()[2] = whole_space_box_min.Z() + k * grid_resolution;
         t.m_index = index;
         t.is_grid_center = true;
         view_grid_points->vert[index] = t;
         view_grid_points->bbox.Add(t.P());
       }
     }
   }
   view_grid_points->vn = max_index;
   cout << "all: " << max_index << endl;
   cout << "x_max: " << x_max << endl;
   cout << "y_max: " << y_max << endl;
   cout << "z_max: " << z_max << endl;

   bool test_field_segment = para->getBool("Test Other Inside Segment");
   if (field_points->vert.empty())
   {
     test_field_segment = false;
     cout << "field points empty" << endl;
   }
   //distinguish the inside or outside grid

   if (test_field_segment)
   {
     GlobalFun::computeAnnNeigbhors(field_points->vert, view_grid_points->vert, 1, false, "runGridNearestIsoPoint");
   }
   else
   {
     GlobalFun::computeAnnNeigbhors(iso_points->vert, view_grid_points->vert, 1, false, "runGridNearestIsoPoint");
   }
   
   for (int i = 0; i < view_grid_points->vert.size(); ++i)
   {
     Point3f &t = view_grid_points->vert[i].P();
     if (!view_grid_points->vert[i].neighbors.empty())
     {
       if (test_field_segment)
       {
         CVertex &nearest = field_points->vert[view_grid_points->vert[i].neighbors[0]];
         if (nearest.eigen_confidence > 0)
         {
           view_grid_points->vert[i].is_ray_stop = true; 
         }
       }
       else
       {
         CVertex &nearest = iso_points->vert[view_grid_points->vert[i].neighbors[0]];
         Point3f &v = nearest.P();
         double dist = GlobalFun::computeEulerDist(t, v);
         Point3f n = nearest.N();
         Point3f l = view_grid_points->vert[i].P() - nearest.P();
         if ((n * l < 0.0f && dist < grid_resolution * 2)
           /*|| (test_other_segment && dist < grid_resolution / 2)*/) //wsh change
         {
           view_grid_points->vert[i].is_ray_stop = true; 
         }  
       }
     }
   }

   if (use_grid_segment)
   {
     for (int i = 0; i < view_grid_points->vert.size(); ++i)
     {
       CVertex &t = view_grid_points->vert[i];

       if (!t.is_ray_stop)
       {
         t.is_skel_ignore = true;
       }
     }
   }
}


void
NBV::propagate()
{
  bool use_average_confidence = para->getBool("Use Average Confidence");
  bool use_propagate_one_point = para->getBool("Run Propagate One Point");
  bool use_max_propagation = para->getBool("Use Max Propagation");

  if (view_grid_points)
  {
    for (int i = 0; i < view_grid_points->vert.size(); i++)
    {
      CVertex& t = view_grid_points->vert[i];
      t.eigen_confidence = 0.0;
      t.N() = Point3f(0., 0., 0.);
      t.weight_sum = 0.0;
    }
  }
  if (nbv_candidates)
  {
    nbv_candidates->vert.clear();
  }

  if (use_average_confidence)
  {
    confidence_weight_sum.assign(view_grid_points->vert.size(), 0.0);
  }
  normalizeConfidence(iso_points->vert, 0);

  double camera_max_dist = global_paraMgr.camera.getDouble("Camera Max Dist");
  int max_steps = static_cast<int>(camera_max_dist / grid_resolution);
  max_steps *= para->getDouble("Max Ray Steps Para"); //wsh

  //traverse all points on the iso surface
  //for (int i = 0; i < 1; ++i)//fix: < iso_points->vert.size()
  int target_index = 425;
  //int target_index = 1009;  
  //for (int i = target_index; i < target_index+1; ++i)//fix: < iso_points->vert.size()  

  int i = 0;
  if (use_propagate_one_point)
  {
    srand(time(NULL)); 
    i = rand() % iso_points->vert.size();
    cout << "propagate one point index: " << i << endl;
  }

  //parallel
  int iso_points_size = iso_points->vert.size();
#ifdef LINKED_WITH_TBB
  tbb::parallel_for(tbb::blocked_range<size_t>(0, iso_points_size), 
    [&](const tbb::blocked_range<size_t>& r)
  {
    for (size_t i = r.begin(); i < r.end(); ++i)
    {
      vector<int> hit_grid_indexes;
      CVertex &v = iso_points->vert[i];
      //t is the ray_start_point
      v.is_ray_hit = true;
      //ray_hit_nbv_grids->vert.push_back(v);

      //get the x,y,z index of each iso_points
      int t_indexX = static_cast<int>( ceil((v.P()[0] - whole_space_box_min.X()) / grid_resolution ));
      int t_indexY = static_cast<int>( ceil((v.P()[1] - whole_space_box_min.Y()) / grid_resolution ));
      int t_indexZ = static_cast<int>( ceil((v.P()[2] - whole_space_box_min.Z()) / grid_resolution ));
      //next point index along the ray, pay attention , index should be stored in double ,used in integer
      double n_indexX, n_indexY, n_indexZ;
      //get the sphere traversal resolution
      double camera_max_dist = global_paraMgr.camera.getDouble("Camera Max Dist");
      //compute the delta of a,b so as to traverse the whole sphere
      double angle_delta = grid_resolution / camera_max_dist;
      //angle_delta *=2;// wsh
      //loop for a, b
      double a = 0.0f, b = 0.0f;
      double l = 0.0f;
      double x = 0.0f, y = 0.f, z = 0.0f;
      //for DDA algorithm
      //int stepX = 0, stepY = 0, stepZ = 0;

      double length = 0.0f;
      double deltaX, deltaY, deltaZ;

      //double half_D = optimal_D / 2.0f;
      double optimal_D = camera_max_dist / 2.0f;
      double half_D = optimal_D / 2.0f; //wsh    
      double half_D2 = half_D * half_D;
      double sigma = global_paraMgr.norSmooth.getDouble("Sharpe Feature Bandwidth Sigma");
      double sigma_threshold = pow(max(1e-8, 1-cos(sigma/180.0*3.1415926)), 2);

      //1. for each point, propagate to all discrete directions
      for (a = 0.0f; a < PI; a += angle_delta)
      {
        l = sin(a); y = cos(a);
        for (b = 0.0f; b < 2 * PI; b += angle_delta)
        {
          //now the propagate direction is Point3f(x, y, z)
          x = l * cos(b); z = l * sin(b);
          //reset the next grid indexes
          n_indexX = t_indexX; n_indexY = t_indexY; n_indexZ = t_indexZ;
          //2. compute the next grid indexes
          length = getAbsMax(x, y, z);
          deltaX = x / length; 
          deltaY = y / length;
          deltaZ = z / length;

          //int hit_stop_time = 0;
          for (int k = 0; k <= max_steps; ++k)     
          {
            n_indexX = n_indexX + deltaX;
            n_indexY = n_indexY + deltaY;
            n_indexZ = n_indexZ + deltaZ;
            int index = round(n_indexX) * y_max * z_max + round(n_indexY) * z_max + round(n_indexZ);

            if (index >= view_grid_points->vert.size())
            {
              break;
            }
            //if the direction is into the model, or has been hit, then stop tracing
            if (view_grid_points->vert[index].is_ray_stop)
            {
              break;
            }

            if (view_grid_points->vert[index].is_ray_hit)  continue;

            //if the grid get first hit 
            view_grid_points->vert[index].is_ray_hit = true;
            //do what we need in the next grid
            NBVGrid &g = (*view_grids)[index];
            //1. set the confidence of the grid center
            CVertex& t = view_grid_points->vert[index];
            //double dist = GlobalFun::computeEulerDist(v.P(), t.P());
            Point3f diff = t.P() - v.P();
            double dist2 = diff.SquaredNorm();
            double dist = sqrt(dist2);

            Point3f view_direction = diff.Normalize();
            double coefficient1 = exp(-(dist - optimal_D) * (dist - optimal_D) / half_D2);
            double coefficient2 = exp(-pow(1-v.N()*view_direction, 2)/sigma_threshold);

            float iso_confidence = 1 - v.eigen_confidence;
            float view_weight = iso_confidence * coefficient2;          

            float confidence_weight = coefficient1 * coefficient2;
            //float confidence_weight = coefficient1;

            if (use_max_propagation)
            {
              //t.eigen_confidence = (std::max)(float(t.eigen_confidence), float(confidence_weight * iso_confidence));          
              if (confidence_weight * iso_confidence > t.eigen_confidence)
              {
                t.eigen_confidence = confidence_weight * iso_confidence;
                t.N() = (v.P()-t.P()).Normalize();
                t.remember_iso_index = v.m_index;
                //t.N() = -v.N();
                //t.m_index = v.m_index;
              }
            }
            else
            {
              t.eigen_confidence += coefficient1 * iso_confidence;
              //t.eigen_confidence += confidence_weight * 1.0;              
            }

            if (use_average_confidence)
            {
              confidence_weight_sum[index] += 1.;
            }

            //confidence_weight_sum[index] += confidence_weight;

            //t.N() += view_direction * view_weight;
            //t.weight_sum += view_weight;

            // record hit_grid center index
            hit_grid_indexes.push_back(index);
          }//end for k
        }// end for b
      }//end for a

      if (hit_grid_indexes.size() > 0)
      {
        setGridUnHit(hit_grid_indexes);
        hit_grid_indexes.clear();
      }

      if (use_propagate_one_point)
      {
        break;
      }
    }//end for iso_points
  });
#else
  for ( ;i < iso_points->vert.size(); ++i)//fix: < iso_points->vert.size()    
  {
    //    cout << "index" << i << endl;
    vector<int> hit_grid_indexes;

    CVertex &v = iso_points->vert[i];
    //t is the ray_start_point
    v.is_ray_hit = true;


    //ray_hit_nbv_grids->vert.push_back(v);

    //get the x,y,z index of each iso_points
    int t_indexX = static_cast<int>( ceil((v.P()[0] - whole_space_box_min.X()) / grid_resolution ));
    int t_indexY = static_cast<int>( ceil((v.P()[1] - whole_space_box_min.Y()) / grid_resolution ));
    int t_indexZ = static_cast<int>( ceil((v.P()[2] - whole_space_box_min.Z()) / grid_resolution ));
    //next point index along the ray, pay attention , index should be stored in double ,used in integer
    double n_indexX, n_indexY, n_indexZ;
    //get the sphere traversal resolution
    double camera_max_dist = global_paraMgr.camera.getDouble("Camera Max Dist");
    //compute the delta of a,b so as to traverse the whole sphere
    double angle_delta = grid_resolution / camera_max_dist;
    //angle_delta *=2;// wsh

    //loop for a, b
    double a = 0.0f, b = 0.0f;
    double l = 0.0f;
    double x = 0.0f, y = 0.f, z = 0.0f;
    //for DDA algorithm
    //int stepX = 0, stepY = 0, stepZ = 0;

    double length = 0.0f;
    double deltaX, deltaY, deltaZ;

    //double half_D = optimal_D / 2.0f;
    double optimal_D = camera_max_dist / 2.0f;
    double half_D = optimal_D / 2.0f; //wsh    
    double half_D2 = half_D * half_D;
    //for debug

    double sigma = global_paraMgr.norSmooth.getDouble("Sharpe Feature Bandwidth Sigma");
    double sigma_threshold = pow(max(1e-8, 1-cos(sigma/180.0*3.1415926)), 2);

    //1. for each point, propagate to all discrete directions
    for (a = 0.0f; a < PI; a += angle_delta)
    {
      l = sin(a); y = cos(a);
      for (b = 0.0f; b < 2 * PI; b += angle_delta)
      {
        //now the propagate direction is Point3f(x, y, z)
        x = l * cos(b); z = l * sin(b);
        //reset the next grid indexes
        n_indexX = t_indexX; n_indexY = t_indexY; n_indexZ = t_indexZ;
        //2. compute the next grid indexes
        length = getAbsMax(x, y, z);
        deltaX = x / length; 
        deltaY = y / length;
        deltaZ = z / length;

        //int hit_stop_time = 0;
        for (int k = 0; k <= max_steps; ++k)
          //for (int k = 0; k <= 100000; ++k)
          //while (1)        
        {
          n_indexX = n_indexX + deltaX;
          n_indexY = n_indexY + deltaY;
          n_indexZ = n_indexZ + deltaZ;
          int index = round(n_indexX) * y_max * z_max + round(n_indexY) * z_max + round(n_indexZ);

          if (index >= view_grid_points->vert.size())
          {
            break;
          }
          //if the direction is into the model, or has been hit, then stop tracing
          if (view_grid_points->vert[index].is_ray_stop)
          {
            break;
          }

          if (view_grid_points->vert[index].is_ray_hit)  continue;

          //if the grid get first hit 
          view_grid_points->vert[index].is_ray_hit = true;
          //do what we need in the next grid
          NBVGrid &g = (*view_grids)[index];
          //1. set the confidence of the grid center
          CVertex& t = view_grid_points->vert[index];
          //double dist = GlobalFun::computeEulerDist(v.P(), t.P());
          Point3f diff = t.P() - v.P();
          double dist2 = diff.SquaredNorm();
          double dist = sqrt(dist2);

          Point3f view_direction = diff.Normalize();
          double coefficient1 = exp(-(dist - optimal_D) * (dist - optimal_D) / half_D2);
          double coefficient2 = exp(-pow(1-v.N()*view_direction, 2)/sigma_threshold);

          float iso_confidence = 1 - v.eigen_confidence;
          float view_weight = iso_confidence * coefficient2;          
            
          if (use_max_propagation)
          {
            t.eigen_confidence = (std::max)(t.eigen_confidence, confidence_weight * float(1.0));          
          }
          else
          {
            //t.eigen_confidence += coefficient1 * iso_confidence;
            t.eigen_confidence += coefficient1 * 1.0;
          }

          float confidence_weight = coefficient1 * coefficient2;
          //float confidence_weight = coefficient1;

          if (use_max_propagation)
          {
            t.eigen_confidence = (std::max)(t.eigen_confidence, confidence_weight * iso_confidence);          
          }
          else
          {
            t.eigen_confidence += coefficient1 * iso_confidence;
          }

          if (use_average_confidence)
          {
            confidence_weight_sum[index] += 1.;
          }

          //confidence_weight_sum[index] += confidence_weight;

          t.N() += view_direction * view_weight;
          t.weight_sum += view_weight;

          // record hit_grid center index
          hit_grid_indexes.push_back(index);
        }//end for k
      }// end for b
    }//end for a

    if (hit_grid_indexes.size() > 0)
    {
      setGridUnHit(hit_grid_indexes);
      hit_grid_indexes.clear();
    }

    if (use_propagate_one_point)
    {
      break;
    }
  }//end for iso_points
#endif

  if (use_average_confidence)
  {
    for (int i = 0; i < view_grid_points->vert.size(); i++)
    {
      CVertex& t = view_grid_points->vert[i];
      //if (t.weight_sum > 1e-10)
      //{
      //  t.N() /= -t.weight_sum;
      //}

      //t.recompute_m_render();

      if (use_average_confidence)
      {
        if (confidence_weight_sum[i] > 5)
        {
          t.eigen_confidence /= confidence_weight_sum[i];
        }
      }

      //t.N() *= -1;
      //t.N().Normalize();
    }
  }

  normalizeConfidence(view_grid_points->vert, 0.);
}

void NBV::normalizeConfidence(vector<CVertex>& vertexes, float delta)
{  
  float min_confidence = GlobalFun::getDoubleMAXIMUM();
  float max_confidence = 0;
  for (int i = 0; i < vertexes.size(); i++)
  {
    CVertex& v = vertexes[i];
    min_confidence = (std::min)(min_confidence, v.eigen_confidence);
    max_confidence = (std::max)(max_confidence, v.eigen_confidence);
  }
  float space = max_confidence - min_confidence;

  for (int i = 0; i < vertexes.size(); i++)
  {
    CVertex& v = vertexes[i];
    v.eigen_confidence = (v.eigen_confidence - min_confidence) / space;
    v.eigen_confidence += delta;
  }

}
double
NBV::getAbsMax(double x, double y, double z)
{
  return std::max(abs(x), std::max(abs(y), abs(z)));
}

int 
NBV::round(double x)
{
  return static_cast<int>(x + 0.5);
}

quadrant
NBV::getQuadrantIdx(double a, double b)
{
  if (a >= 0 && a <= PI / 2)
  {
    if (b >=0 && b <= PI / 2)         return First;
    if (b > PI / 2 && b < PI)         return Second;
    if (b > PI && b < 3 / 2 * PI)     return Third;
    if (b > 3 / 2 * PI && b < 2 * PI) return Fourth;
  }
  if (a >PI / 2 && a <= PI)
  {
    if (b >=0 && b <= PI / 2)         return Fifth;
    if (b > PI / 2 && b < PI)         return Sixth;
    if (b > PI && b < 3 / 2 * PI)     return Seventh;
    if (b > 3 / 2 * PI && b < 2 * PI) return Eighth;
  }
}

void
NBV::setGridUnHit(vector<int>& hit_grids_idx)
{
  vector<int>::iterator it;
  for (it = hit_grids_idx.begin(); it != hit_grids_idx.end(); ++it)
  {
    view_grid_points->vert[*it].is_ray_hit = false;
  }
}

void 
NBV::viewExtraction()
{
  double nbv_confidence_value = para->getDouble("Confidence Separation Value");
  nbv_candidates->vert.clear();

  int index = 0;
  for (int i = 0; i < view_grid_points->vert.size(); i++)
  {
    CVertex& v = view_grid_points->vert[i];
    if (v.eigen_confidence > nbv_confidence_value)
    {
      v.m_index = index++;
      //v.is_grid_center = false;
      //v.is_iso = true;
      nbv_candidates->vert.push_back(v);
    }
  }
  nbv_candidates->vn = nbv_candidates->vert.size();
  cout << "candidate number: " << nbv_candidates->vn << endl;
}

void
NBV::viewExtractionIntoBins()
{
  nbv_candidates->vert.clear();
  Point3f diff = whole_space_box_max - whole_space_box_min;
  double bin_length_x = diff.X() / NBV::view_bins_each_axis;
  double bin_length_y = diff.Y() / NBV::view_bins_each_axis;
  double bin_length_z = diff.Z() / NBV::view_bins_each_axis;
  
  //dynamic allocate memory
  int bin_confidence_size = NBV::view_bins_each_axis * NBV::view_bins_each_axis * NBV::view_bins_each_axis;
  float *bin_confidence = new float[bin_confidence_size];
  memset(bin_confidence, 0, bin_confidence_size * sizeof(float));

  int ***view_bins;
  view_bins = new int **[NBV::view_bins_each_axis];
  for (int i = 0; i < NBV::view_bins_each_axis; ++i)
  {
    view_bins[i] = new int *[NBV::view_bins_each_axis];
    for (int j = 0; j < NBV::view_bins_each_axis; ++j)
    {
      view_bins[i][j] = new int[NBV::view_bins_each_axis];
    }
  }
  
  //process each iso_point
  int index = 0;
  for (int i = 0; i < view_grid_points->vert.size(); ++i)
  {
    CVertex &v = view_grid_points->vert[i];
    
    //get the x,y,z index of each iso_points
    int t_indexX = static_cast<int>( floor((v.P()[0] - whole_space_box_min.X()) / bin_length_x ));
    int t_indexY = static_cast<int>( floor((v.P()[1] - whole_space_box_min.Y()) / bin_length_y ));
    int t_indexZ = static_cast<int>( floor((v.P()[2] - whole_space_box_min.Z()) / bin_length_z ));

    t_indexX = t_indexX >= NBV::view_bins_each_axis ? (NBV::view_bins_each_axis-1) : t_indexX;
    t_indexY = t_indexY >= NBV::view_bins_each_axis ? (NBV::view_bins_each_axis-1) : t_indexY;
    t_indexZ = t_indexZ >= NBV::view_bins_each_axis ? (NBV::view_bins_each_axis-1) : t_indexZ;

    int idx = t_indexX * NBV::view_bins_each_axis * NBV::view_bins_each_axis 
                  + t_indexY * NBV::view_bins_each_axis + t_indexZ;

    if (v.eigen_confidence > bin_confidence[idx])
    {
      bin_confidence[idx] = v.eigen_confidence;
      view_bins[t_indexX][t_indexY][t_indexZ] = v.m_index;
    }
  }

  //put bin direction into nbv_candidates
  for (int i = 0; i < NBV::view_bins_each_axis; ++i)
  {
    for (int j = 0; j < NBV::view_bins_each_axis; ++j)
    {
      for (int k = 0; k < NBV::view_bins_each_axis; ++k)
      {
        nbv_candidates->vert.push_back(view_grid_points->vert[view_bins[i][j][k]]);
      }
    }
  }
  nbv_candidates->vn = nbv_candidates->vert.size();
  cout << "candidate number: " << nbv_candidates->vn << endl;

  //release the memory
  delete [] bin_confidence;

  for (int i = 0; i < NBV::view_bins_each_axis; ++i)
  {
    for (int j = 0; j < NBV::view_bins_each_axis; ++j)
    {
      delete[] view_bins[i][j];
    }
    delete view_bins[i];
  }
  delete view_bins;
}

void NBV::viewClustering()
{
  //double radius = para->getDouble("CGrid Radius"); 
  double radius = global_paraMgr.wLop.getDouble("CGrid Radius"); 
  
  double radius2 = radius * radius;
  double iradius16 = -4/radius2;

  double sigma = 45;
  double cos_sigma = cos(sigma / 180.0 * 3.1415926);
  double sharpness_bandwidth = std::pow((std::max)(1e-8, 1 - cos_sigma), 2);

  GlobalFun::computeBallNeighbors(nbv_candidates, NULL, 
                                  radius, nbv_candidates->bbox);
  //GlobalFun::computeAnnNeigbhors(nbv_candidates->vert,
  //  nbv_candidates->vert, 
  //  15,
  //  false,
  //  "runViewCandidatesClustering");

  vector<CVertex> update_temp;
  for(int i = 0; i < nbv_candidates->vert.size(); i++)
  {
    CVertex& v = nbv_candidates->vert[i];

    //if (v.neighbors.size() <= 5)
    if (v.neighbors.empty())
    {
      //update_temp.push_back(v);
      continue;
    }

    Point3f average_positon = Point3f(0, 0, 0);
    Point3f average_normal = Point3f(0, 0, 0);
    double sum_weight = 0.0;

    for (int j = 0; j < v.neighbors.size(); j++)
    {
      CVertex& t = nbv_candidates->vert[v.neighbors[j]];

      Point3f diff = v.P() - t.P();
      double dist2  = diff.SquaredNorm();

      double dist_weight = exp(dist2 * iradius16);
      double normal_weight = exp(-std::pow(1 - v.N() * t.N(), 2));
      double weight = dist_weight;

      average_positon += t.P() * weight;
      average_normal += t.N() * weight;
      sum_weight += weight;
    }

    CVertex temp_v = v;
    temp_v.P() = average_positon / sum_weight;
    temp_v.N() = average_normal / sum_weight;
    update_temp.push_back(temp_v);
  }

  nbv_candidates->vert.clear();
  for (int i = 0; i < update_temp.size(); i++)
  {
    nbv_candidates->vert.push_back(update_temp[i]);
  }
  nbv_candidates->vn = nbv_candidates->vert.size(); 
}


double NBV::computeLocalScores(CVertex& view_t, CVertex& iso_v, 
                               double& optimal_D, double& half_D2, double& sigma_threshold)
{
  //return (1.0-iso_v.eigen_confidence);

  double sum_weight = 0.0;
  //iso_v.neighbors.push_back(iso_v.m_index);
  double max_confidence = 0.0;

  for(int i = 0; i < iso_v.neighbors.size(); i++)
  {
    int neighbor_index = iso_v.neighbors[i];
    CVertex& t = iso_points->vert[neighbor_index];

    Point3f diff = view_t.P()-t.P();
    double dist2 = diff.SquaredNorm();
    double dist = sqrt(dist2);
    Point3f view_direction = diff.Normalize();

    double w1 = exp(-(dist - optimal_D) * (dist - optimal_D) / half_D2);
    //double w1 = 1.0;    
    double w2 = exp(-pow(1-t.N()*view_direction, 2)/sigma_threshold);
    //double w2 = 1.0;
    double weight = w1 * w2 * (1.0-t.eigen_confidence); 
    sum_weight += weight;
    if (t.eigen_confidence < max_confidence)
    {
      max_confidence = t.eigen_confidence;
    }
  }

  //return sum_weight * max_confidence;

  return sum_weight;

  //return sum_weight / iso_v.neighbors.size();
  //return max_weight;

}

void NBV::setIsoBottomConfidence()
{
	if (iso_points == NULL) 
	{
		cout<<"iso_points empty!"<<endl;
		return;
	}

	Point3f bbox_min = iso_points->bbox.min;
	double bottom_delta = global_paraMgr.nbv.getDouble("Iso Bottom Delta");
	for (int i = 0; i < iso_points->vert.size(); ++i)
	{
		CVertex &v = iso_points->vert[i];
		if (v.P().X() < bbox_min.X() + bottom_delta)
		{
			v.eigen_confidence = 1.0f;
		}
	}
}

void NBV::updateViewDirections()
{
  cout << "NBV::updateViewDirections" << endl;
  normalizeConfidence(iso_points->vert, 0.0);

  double radius = global_paraMgr.wLop.getDouble("CGrid Radius"); 
  radius *= 2.0;
  //double radius = 0.3;

  double camera_max_dist = global_paraMgr.camera.getDouble("Camera Max Dist");

  double optimal_D = camera_max_dist / 2.0f;
  double half_D = optimal_D / 2.0f; //wsh    
  double half_D2 = half_D * half_D;
  double sigma = global_paraMgr.norSmooth.getDouble("Sharpe Feature Bandwidth Sigma");
  double sigma_threshold = pow(max(1e-8, 1-cos(sigma/180.0*3.1415926)), 2);

  double radius2 = radius * radius;
  double iradius16 = -4/radius2;

  GlobalFun::computeBallNeighbors(iso_points, NULL, 
                                  radius, iso_points->bbox);
  for (int i = 0; i < iso_points->vert.size(); i++)
  {
    CVertex& v = iso_points->vert[i];
    v.neighbors.push_back(v.m_index);
    v.m_index = i;
  }

  for (int i = 0; i < nbv_candidates->vert.size(); i++)
  //for (int i = 0; i < 3; i++)  
  {
    CVertex& nbvc = nbv_candidates->vert[i];
    int iso_index = nbvc.remember_iso_index;
    if (iso_index < 0)
    {
      continue;
    }

    CVertex& iso_v = iso_points->vert[iso_index];

    float max_score = 0.;
    int best_iso_index = -1.;

    //double v_score = computeLocalScores(nbvc, iso_v, optimal_D, half_D2, sigma_threshold);
    //max_score = v_score;
    //best_iso_index = iso_index;


    for (int j = 0; j < iso_v.neighbors.size(); j++)
    {
      int neighbor_index = iso_v.neighbors[j];

      CVertex& t = iso_points->vert[neighbor_index];
      
      double t_score = computeLocalScores(nbvc, t, optimal_D, half_D2, sigma_threshold);

      if (t_score > max_score)
      {
        max_score = t_score;
        best_iso_index = neighbor_index;
      }
    }

    cout << "Max scores:  " << max_score << endl;
    Point3f best_direction_pos = iso_points->vert[best_iso_index].P();
    nbvc.N() = (best_direction_pos - nbvc.P()).Normalize();
    nbvc.remember_iso_index = best_iso_index;
  }

}

//void NBV::updateViewDirections()
//{
//  cout << "NBV::updateViewDirections" << endl;
//
//  double radius = global_paraMgr.wLop.getDouble("CGrid Radius"); 
//  radius *= 3.0;
//
//  double radius2 = radius * radius;
//  double iradius16 = -4/radius2;
//
//  GlobalFun::computeBallNeighbors(iso_points, NULL, 
//                                  radius, iso_points->bbox);
//
//  for (int i = 0; i < nbv_candidates->vert.size(); i++)
//  {
//    CVertex& nbvc = nbv_candidates->vert[i];
//    int iso_index = nbvc.remember_iso_index;
//    if (iso_index < 0)
//    {
//      continue;
//    }
//
//    CVertex& v = iso_points->vert[i];
//
//    float mini_sum_confidence = GlobalFun::getDoubleMAXIMUM();
//    int best_iso_index = -1;
//    float sum_confidence = 0.0;
//    for (int j = 0; j < v.neighbors.size(); j++)
//    {
//      int neighbor_index = v.neighbors[j];
//      CVertex& t = iso_points->vert[neighbor_index];
//      sum_confidence += t.eigen_confidence;
//    }
//    mini_sum_confidence = sum_confidence;
//    best_iso_index = iso_index;
//
//
//    for (int j = 0; j < v.neighbors.size(); j++)
//    {
//      int neighbor_index = v.neighbors[j];
//      CVertex& t = iso_points->vert[neighbor_index];
//      float neighbor_sum_confidence = 0.;
//
//      for (int k = 0; k < t.neighbors.size(); k++)
//      {
//        int neighbor_neighbor_index = t.neighbors[k];
//        CVertex& u = iso_points->vert[neighbor_neighbor_index];
//        neighbor_sum_confidence += u.eigen_confidence;
//      }
//
//      if (neighbor_sum_confidence < mini_sum_confidence)
//      {
//        mini_sum_confidence = neighbor_sum_confidence;
//        best_iso_index = neighbor_index;
//      }
//    }
//
//    Point3f best_direction_pos = iso_points->vert[best_iso_index];
//    nbvc.N() = (best_direction_pos - nbvc.P()).Normalize();
//  }
//
//}
